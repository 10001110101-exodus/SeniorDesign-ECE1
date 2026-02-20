# the transmitting end of the simulation 
# will read from an output file, send data, and recieve acknowledgements

# imports
import socket
import random
import time
import struct
import argparse

# defined byte lengths
DATA_PCK_LEN = 32
DATA_BYTES = 31
ACK_LEN = 2

# connecting RX and TX
RX_HOST = "127.0.0.1"
RX_PORT = 9000                 
TX_ACK_HOST = "127.0.0.1"
TX_ACK_PORT = 9001              

# stop and wait settings
MAX_RETRIES = 5
RTT_TIMEOUT_MS = 1000

# simulation factors
LOSS_DATA_PROB = 0.15
DATA_DELAY_MS_MIN = 300
DATA_DELAY_MS_MAX = 400 

# define the randomness of the simulation
SEED = 12

# function to pick a random integer to delay in between min and max
def random_delay(min_ms: int, max_ms: int):
    time.sleep(random.randint(min_ms, max_ms) / 1000.0)

# ensure the data in the packet is exactly 31 bytes and return the 32 byte packet 
def make_packet(pck_num: int, data: bytes) -> bytes:
    if len(data) != DATA_BYTES:
        raise ValueError("Data must be exactly 31 bytes")
    return bytes([pck_num & 0xFF]) + data 

# returns status byte if ACK for expected pack number arrives else return None on the timeout
def wait_for_ack(ack_sock: socket.socket, expected_pck_num: int) -> int | None:
    while True:
        # block out waiting for the ACK until the timeout and if timeout then return None
        try:
            ack, addr = ack_sock.recvfrom(1024)
        except socket.timeout:
            return None
        
        # ignore werid acknowledgements
        if len(ack) != ACK_LEN:
            continue
        
        # get the packet number and the status 
        pck_num , status = ack[0], ack[1]

        # ignore acknowledgements for the wrong packet number (things could get out of sequence)
        if pck_num != (expected_pck_num & 0xFF):
            continue

        return status
    
# send one packet with up to 5 retries
def send_with_retries(tx_sock, ack_sock, pck_num: int, pck: bytes) -> bool:
    for attempt in range(1, MAX_RETRIES + 1):
        # simulate a packet being dropped or sent 
        if random.random() < LOSS_DATA_PROB:
            print(f"TX: PCK {pck_num} DROPPED attempt {attempt}")
            pass
        else:
            random_delay(DATA_DELAY_MS_MIN, DATA_DELAY_MS_MAX)
            tx_sock.sendto(pck, (RX_HOST, RX_PORT))
            print(f"TX: TRYING TO SEND PCK {pck_num} attempt {attempt}")

        # wait for acknowledgement 
        status = wait_for_ack(ack_sock, pck_num) 
        
        # if there is a timeout, retry
        if status is None:
            print(f"TX: packet number = {pck_num} timeout. Retry {attempt}/{MAX_RETRIES}")
            continue

        # finish trying to send if the status is 0 or 1
        if status in (0, 1):
            # 0 = everything's okay, 1 = a duplicate 
            if status == 0:
                print(f"TX: packet number = {pck_num} delivered SUCCESFULLY. Attempts = {attempt}")
            else:
                print(f"TX: packet number = {pck_num} receiver says DUPLICATE")
            return True
        
        # status 2 means a bad acknowledgement length so retry
        print(f"TX: packet number = {pck_num} receiver status={status} -> RETRY")
    
    # failed after 5 attempts
    print(f"TX: packet number = {pck_num} FAILED after {MAX_RETRIES} retries")
    return False


# convert a file into 31-byte chuncks
# first data chunk : 4 byte little-endian file length + 27 bytes of data (padded with zeros if needed)
# remaining data chunks: 31 bytes of data (padded with zeros at end)
def data_for_file(path: str):
    with open(path, "rb") as f:
        data_r = f.read()
    total_len = len(data_r)

    # first data chunk
    first_data_only_data = data_r[:27]
    first_data = struct.pack("<I", total_len) + first_data_only_data
    first_data = first_data.ljust(31, b"\x00")               # pad with zeros 
    yield first_data

    # handle the rest of the file 
    offset = 27
    while offset < total_len:
        data = data_r[offset: offset + 31]
        data = data.ljust(31, b"\x00")                       # pad with zeros
        yield data
        offset += 31


def main():
    # setup so we can input the file
    parser = argparse.ArgumentParser()
    parser.add_argument("input_file")

    parser.add_argument("--rx-host", default="127.0.0.1")
    parser.add_argument("--rx-port", type=int, default=9000)
    parser.add_argument("--tx-ack-host", default="127.0.0.1")
    parser.add_argument("--tx-ack-port", type=int, default=9001)

    parser.add_argument("--seed", type=int, default=12)
    parser.add_argument("--timeout-ms", type=int, default=1000)
    parser.add_argument("--retries", type=int, default=5)

    parser.add_argument("--loss-data", type=float, default=0.15)
    parser.add_argument("--data-delay-min", type=int, default=300)
    parser.add_argument("--data-delay-max", type=int, default=400)

    args = parser.parse_args()

    # override sim settings from CLI
    global MAX_RETRIES, RTT_TIMEOUT_MS, LOSS_DATA_PROB, DATA_DELAY_MS_MIN, DATA_DELAY_MS_MAX
    MAX_RETRIES = args.retries
    RTT_TIMEOUT_MS = args.timeout_ms
    LOSS_DATA_PROB = args.loss_data
    DATA_DELAY_MS_MIN = args.data_delay_min
    DATA_DELAY_MS_MAX = args.data_delay_max

    # also honor ports/hosts
    global RX_HOST, RX_PORT, TX_ACK_HOST, TX_ACK_PORT
    RX_HOST = args.rx_host
    RX_PORT = args.rx_port
    TX_ACK_HOST = args.tx_ack_host
    TX_ACK_PORT = args.tx_ack_port


    random.seed(args.seed)

    # create the socket
    tx_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ack_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ack_sock.bind((TX_ACK_HOST, TX_ACK_PORT))
    
    # set the timeout
    ack_sock.settimeout(RTT_TIMEOUT_MS / 1000.0)

    print(f"TX: sending {args.input_file} on {RX_HOST}:{RX_PORT}")
    print(f"TX: listening for ACKs on {TX_ACK_HOST}:{TX_ACK_PORT}")

    pck_num = 0
    sent_data_chunks = 0
    success_data_chunks = 0

    # try transmit all the data 
    for data_chunk in data_for_file(args.input_file):
        pck = make_packet(pck_num, data_chunk)          # build a packet
        success = send_with_retries(tx_sock, ack_sock, pck_num, pck)        # try send 
        sent_data_chunks += 1

        # if not successful just stop
        if not success:
            print(f"TX: stopping early due to repeated FAILURES")
            break

        # otherwise increment the success data chunks and alternate the bit
        success_data_chunks += 1
        pck_num ^= 1

    print("\n TX: DONE")
    print(f"Data chunks sent: {sent_data_chunks}, chunks successfully delivered: {success_data_chunks}")



if __name__ == "__main__":
    main()