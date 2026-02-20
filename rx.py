# the receiving end of the simulation 
# will write to an output file and send acknowledgements 

# imports
import socket
import random
import time
import struct
import argparse


# defined byte lengths
DATA_PCK_LEN = 32
DATA_BYTES = 31
ACK_LN = 2


# connecting RX and TX
RX_HOST = "127.0.0.1"
RX_PORT = 9000                  # RX listens here for DATA
TX_ACK_PORT = 9001              # TX listens here for ACKs

# simulation factors
LOSS_ACK_PROB = 0.08            # set the probability that an ACK gets lost
ACK_DELAY_MS_MIN = 25           # set the minimum delay for sending an ACK
ACK_DELAY_MS_MAX = 40           # set the maximum delay for sending and ACK

# define the randomness of the simulation
SEED = 12


def random_delay(min_ms: int, max_ms: int):
    # function to pick a random integer to delay in between min and max
    time.sleep(random.randint(min_ms, max_ms) / 1000.0)

def main():
    
    parser = argparse.ArgumentParser()
    parser.add_argument("--rx-host", default="127.0.0.1")
    parser.add_argument("--rx-port", type=int, default=9000)
    parser.add_argument("--tx-ack-port", type=int, default=9001)
    parser.add_argument("--out", default="received.bin")

    parser.add_argument("--seed", type=int, default=12)
    parser.add_argument("--loss-ack", type=float, default=0.08)
    parser.add_argument("--ack-delay-min", type=int, default=25)
    parser.add_argument("--ack-delay-max", type=int, default=40)

    args = parser.parse_args()


    # override sim settings from CLI
    global RX_HOST, RX_PORT, TX_ACK_PORT, LOSS_ACK_PROB, ACK_DELAY_MS_MIN, ACK_DELAY_MS_MAX
    RX_HOST = args.rx_host
    RX_PORT = args.rx_port
    TX_ACK_PORT = args.tx_ack_port
    LOSS_ACK_PROB = args.loss_ack
    ACK_DELAY_MS_MIN = args.ack_delay_min
    ACK_DELAY_MS_MAX = args.ack_delay_max

    random.seed(args.seed)
    
    
    # create an output file to reconstruct the transmitted data in binary 
    out_path = args.out
    out_f = open(out_path, "wb")

    # connect to the socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((RX_HOST, RX_PORT))
    print(f"RX listening on {RX_HOST}: {RX_PORT}")
    print(f"RX writing reconstructed data to: {out_path}")

    last_delivered_pck_num = None              # used to detect duplicates
    expected_total_len = None                   # total file size 
    bytes_written = 0                           # how many bytes have been written so far

    done = False

    # get a packet
    while True:
        # pck raw bytes and addr is the sender address/ port
        pck, addr = sock.recvfrom(4096)

        # make sure we get 32 bytes
        if len(pck) != DATA_PCK_LEN:
            # if there is at least one byte treat it as the packet number
            pck_num = pck[0] if len(pck) >= 1 else 0
            
            # create an ack that gives the sequence and the status of a bad len
            status = 2      # use 2 to indicate bad length
            ack = bytes([pck_num & 0xFF, status])    

            # based on the probability of loosing an ACK send the ACK
            if random.random() >= LOSS_ACK_PROB:
                random_delay(ACK_DELAY_MS_MIN, ACK_DELAY_MS_MAX)
                sock.sendto(ack, (RX_HOST, TX_ACK_PORT))
                print(f"RX: bad length {len(pck)} from {addr} -> ACK(BAD_LEN)")
            else:
                print(f"RX: bad length {len(pck)} from {addr} -> ACK DROPPED")
            continue

        # otherwise we have a packet of the right length so process it
        pck_num = pck[0]
        data = pck[1:]          # 31 bytes

        # check if we already accepted this packet number (a resend)
        if last_delivered_pck_num == pck_num:
            status = 1          # use 1 to indicate a duplicate
            print(f"RX: duplicate packet number = {pck_num} -> ACK(DUPLICATE)")
        # otherwise it is new
        else:
            status = 0                          # use 0 to indicate everything's okay
            last_delivered_pck_num = pck_num

            # process the very first data chunk 
            if expected_total_len is None:
                # expected that the first 4 bytes are the file length
                expected_total_len = struct.unpack("<I", data[:4])[0]
                actual_data = data[4:]          # 27 bytes remaining 
                
                # make sure not to write more than the file's actual size
                to_write = actual_data[: max(0, expected_total_len - bytes_written)]

                # write bytes to new file and update counter
                out_f.write(to_write)
                bytes_written += len(to_write)
                print(f"RX: accepted packet number = {pck_num} (first) file_len = {expected_total_len} wrote = {len(to_write)}")

            # else process all the remaining bytes 
            else:
                to_write = data[: max(0, expected_total_len - bytes_written)]
                out_f.write(to_write)
                bytes_written += len(to_write)
                print(f"RX: accepted packet number = {pck_num} wrote={len(to_write)} total={bytes_written}/{expected_total_len}")

            # if finished close the file and stop
            if expected_total_len is not None and bytes_written >= expected_total_len:
                print("RX: file COMPLETE, closing output")
                out_f.close()
                done = True
            else:
                done = False
        
        # send the acknowledgement 
        ack = bytes([pck_num & 0xFF, status])

        # randomly simulate the ack being lost 
        ack_dropped = random.random() < LOSS_ACK_PROB
        if ack_dropped:                
            print(f"RX: ACK DROPPED (packet number = {pck_num}), status = {status}")        
        # otherwise send with a delay
        else:
            random_delay(ACK_DELAY_MS_MIN, ACK_DELAY_MS_MAX)
            sock.sendto(ack, (RX_HOST, TX_ACK_PORT))
            print(f"RX: ACK SUCCESSFULLY SENT (packet number = {pck_num}), status = {status}")
        # break out of the receieve loop
        if done and not ack_dropped:
            break


if __name__ == "__main__":
    main()
