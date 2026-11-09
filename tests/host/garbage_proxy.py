import socket, threading, sys
ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ls.bind(('127.0.0.1', 19363)); ls.listen(1)
conn, _ = ls.accept()
up = socket.create_connection(('127.0.0.1', 19361))
conn.sendall(b'\x00\xff\x13GARBAGE-BYTES-BEFORE-STREAM-KD')  # 33 garbage bytes, ends with partial magic 'KD'
while True:
    d = up.recv(65536)
    if not d: break
    conn.sendall(d)
conn.close(); ls.close()
