import os
import time
from pathlib import Path
from typing import List, Union


class Pipe:
    def __init__(self, name: str, create=True):
        self.name = name
        self.pipe_path = Path(name)
        if self.pipe_path.exists() and create:
            self.pipe_path.unlink()

        if create:
            os.mkfifo(self.name)

        while True:
            try:
                self.pipe_fd = os.open(
                    self.pipe_path, os.O_RDONLY | os.O_NONBLOCK
                )
                break
            except Exception:
                time.sleep(1e-6)

    def write(self, message: Union[str, bytes]) -> None:
        with open(self.pipe_path, mode="wb") as fifo:
            if type(message) == str:
                fifo.write((message + "\n").encode())
            elif type(message) == bytes:
                fifo.write(f"{message}\n".encode()) 
                # fifo.write(message + b"\n") may be better speaking of overhead 아마?            
            fifo.flush()

    def read(self, busy_wait=True) -> List[str]:
        while True:
            time.sleep(1e-6)

            response = ""
            try:
                response = os.read(self.pipe_fd, 4096).decode().strip()
                if response:
                    break

                if not busy_wait:
                    break

            except Exception as e:
                pass

        return response.splitlines()
