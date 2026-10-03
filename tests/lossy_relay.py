# Forward RTP from :in to :out, dropping a fraction of packets (simulates a lossy WFB link).
import socket, sys, random
inp, out, loss = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3])
random.seed(1)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('127.0.0.1', inp))
o = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
n = d = 0
while True:
    data, _ = s.recvfrom(65535); n += 1
    if random.random() < loss: d += 1; continue
    o.sendto(data, ('127.0.0.1', out))
