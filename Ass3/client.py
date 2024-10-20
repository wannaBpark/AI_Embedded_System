import argparse
import os
import logging
import time
from pathlib import Path
from pipe import Pipe
from typing import List
logging.basicConfig(
    level=logging.INFO,
    format="%(message)s",
)
logger = logging.getLogger(__name__)

class Client:
    def __init__(self, model_name, period, deadline):
        # Initialize class variables
        self.model_name = model_name
        self.period = period / 1000  # Convert to milliseconds
        self.deadline = deadline / 1000  # Convert to milliseconds

        self.client_id = os.getpid()
        self.pipe = Pipe(f"/tmp/pipe_{self.client_id}")
        self.scheduler_pipe_path = Path("/tmp/scheduler_pipe")

        # Check if the scheduler pipe exists
        if not self.scheduler_pipe_path.exists():
            raise FileNotFoundError(
                f"Scheduler pipe {self.scheduler_pipe_path} does not exist"
            )

        self.scheduler_pipe = Pipe(self.scheduler_pipe_path, create=False)
        self.image_file = "image.jpg"
# send, receive만 짜면됨
    def send_request(self): 
        # Send the request
        # For image file, send requests only for the given `self.image_file`.
        request_time = time.time()
        data : str
        data = f"{self.client_id} {self.model_name} {self.image_file} {self.period} {request_time + self.deadline} {request_time}" 
        self.scheduler_pipe.write(data)
        print(f"[Client] pid : {self.client_id} sent request")
        # aboslute deadline = request_time + self.deadline
        # 을 통해 deadline 필드를 바꿔주어 scheduler에 넘겨준다.
        return

    def receive_response(self):
        # Receive the response from the scheduler
        # result : List[str]
        result = self.pipe.read(busy_wait = True)
        print(f"[Client] pid : {self.client_id} received response : {result}")
        return result

    def run(self):
        # Run the client process
        os.makedirs("log/", exist_ok=True)
        log_file = f"log/client_{self.client_id}.log"

        next_request_time = time.time()
        try:
            with open(log_file, "w") as f:
                r_cnt = 0
                while True:
                    time.sleep(1e-6)
                    if time.time() < next_request_time:
                        continue

                    send_time = time.time()
                    self.send_request()
                    response = self.receive_response()
                    cls, score = response[0].split(",")
                    response_time = time.time() - send_time
                    response_time_ms = response_time * 1000

                    result = (
                        "success" if response_time < self.deadline else "fail"
                    )

                    logger.info(
                        f"[Client] pid: {self.client_id} Request {r_cnt + 1}: Result={result}, Response time: {response_time_ms:.3f}ms, Class: {cls}, Score: {score}"
                    )
                    f.write(
                        f"{result},{response_time_ms:.3f}ms,{cls},{score}\n"
                    )
                    r_cnt += 1

                    next_request_time += self.period
        finally:
            # Clean up pipe file when process ends
            try:
                if os.path.exists(f"/tmp/pipe_{self.client_id}"):
                    os.remove(f"/tmp/pipe_{self.client_id}")
                    logger.info(
                        f"[Client] Removed pipe /tmp/pipe_{self.client_id}"
                    )
            except Exception as e:
                logger.error(f"[Client] Error while removing pipe: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Client")
    parser.add_argument(
        "--model_name",
        type=str,
        required=True,
        help="Model name (mobilenet_v2, inception_v2, efficientnet_m)",
    )
    parser.add_argument(
        "--period",
        type=float,
        required=True,
        help="period in milliseconds",
    )
    parser.add_argument(
        "--deadline",
        type=float,
        required=True,
        help="deadline in milliseconds",
    )

    args = parser.parse_args()
    client= Client(args.model_name, args.period,args.deadline)
    client.run()
