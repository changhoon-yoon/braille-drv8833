"""MJPEG 라이브 뷰어 — 브라우저에서 카메라 시점과 인식 상태를 실시간 확인.

reader.py --camera --view 로 켜면 http://<파이IP>:8501 에서 보인다.
표준 multipart/x-mixed-replace 스트림이라 별도 플레이어가 필요 없다.
"""
import http.server
import threading


class Streamer:
    def __init__(self, port=8501):
        self._frame = None
        self._cond = threading.Condition()
        self._srv = http.server.ThreadingHTTPServer(("0.0.0.0", port), self._handler())
        threading.Thread(target=self._srv.serve_forever, daemon=True).start()
        self.port = port

    def publish(self, jpeg_bytes):
        with self._cond:
            self._frame = jpeg_bytes
            self._cond.notify_all()

    def _handler(streamer):
        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass  # 접속 로그로 콘솔이 지저분해지는 것 방지

            def do_GET(self):
                if self.path == "/":
                    # 서버가 재시작되면 MJPEG은 마지막 프레임에서 멈추므로,
                    # 3초마다 /ping을 확인해 끊김→복구 시 자동 새로고침한다.
                    body = (b"<html><head><title>braille reader view</title></head>"
                            b"<body style='margin:0;background:#111;display:flex;"
                            b"justify-content:center'><img src='/stream' "
                            b"style='max-width:100vw;max-height:100vh'>"
                            b"<script>let down=false;setInterval(async()=>{try{"
                            b"await fetch('/ping',{cache:'no-store'});"
                            b"if(down)location.reload();down=false;}"
                            b"catch(e){down=true;}},3000);</script>"
                            b"</body></html>")
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                elif self.path == "/ping":
                    self.send_response(200)
                    self.send_header("Content-Length", "2")
                    self.end_headers()
                    self.wfile.write(b"ok")
                elif self.path == "/stream":
                    self.send_response(200)
                    self.send_header("Content-Type",
                                     "multipart/x-mixed-replace; boundary=frame")
                    self.end_headers()
                    try:
                        while True:
                            with streamer._cond:
                                streamer._cond.wait(timeout=1.0)
                                frame = streamer._frame
                            if frame is None:
                                continue
                            self.wfile.write(b"--frame\r\n"
                                             b"Content-Type: image/jpeg\r\n\r\n")
                            self.wfile.write(frame)
                            self.wfile.write(b"\r\n")
                    except (BrokenPipeError, ConnectionResetError):
                        pass
                else:
                    self.send_response(404)
                    self.end_headers()

        return Handler
