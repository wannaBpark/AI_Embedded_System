import argparse
import logging
import os
from pathlib import Path
import threading
import time

import numpy as np
from PIL import Image

from pycoral.utils.dataset import read_label_file
from pycoral.adapters import common
from pycoral.adapters import classify

import tflite_runtime.interpreter as tflite
from pipe import Pipe

_EDGETPU_SHARED_LIB = "libedgetpu.so.1"

models = {
    "mobilenet_v2": {
        "path": "mobilenet_v2_1.0_224_quant_edgetpu.tflite",
        "wcet": 75,
    },
    "inception_v2": {
        "path": "inception_v2_224_quant_edgetpu.tflite",
        "wcet": 105,
    },
    "efficientnet_m": {
        "path": "efficientnet-edgetpu-M_quant_edgetpu.tflite",
        "wcet": 180,
    },
}

logging.basicConfig(
    level=logging.INFO,
    format="%(message)s",
)
logger = logging.getLogger(__name__)

class Scheduler:
    def __init__(self, algorithm):
        self.algorithm = algorithm  # Scheduling algorithm (FIFO, RM, EDF)
        self.pipe_path = Path("/tmp/scheduler_pipe")
        self.pipe = Pipe(str(self.pipe_path))
        self.lock = threading.Lock()
        self.labels = read_label_file("imagenet_labels.txt")
        self.interpreters = self.load_interpreters()
        self.ready_queue = []

	# 3개의 모델에 대한 interpreter, load
    def load_interpreters(self):
        # Load and initialize the interpreters for each model
        interpreters = {}
        for name, info in models.items():
            # Create Edge TPU delegate
            delegates = [tflite.load_delegate(_EDGETPU_SHARED_LIB)]
            # Make the interpreter with the target model and the delegate
            interpreter = tflite.Interpreter(
                model_path=info['path'], experimental_delegates=delegates
            )
            # Allocate tensor buffers
            interpreter.allocate_tensors()
            interpreters[name] = interpreter
        return interpreters
	# client로부터 오는 request loop 돌면서 처리 : listening thread, 최종적으로는 ready queue에 추가
    def receive_request_loop(self):
        # Continuously read requests from the pipe
        # Parse each request and add it to the ready queue
        while True:
            try:
                time.sleep(1e-6)
                requests = self.pipe.read(busy_wait=False)
                if requests:
                    for request in requests:
                        try:
                            client_id, model_name, image_file, period, deadline, request_time = request.split()
                            request = [client_id, model_name, image_file, float(period), float(deadline), float(request_time)]
                            self.lock.acquire()
                            self.ready_queue.append(request)
                            self.lock.release()
                            print(f"[Scheduler] Received request: {request}")  
                        except Exception as e:
                            print(f"Error reading from pipe from client: {e}")
            except Exception as e:
                print(f"Error reading from pipe from client2: {e}")
        
	# 스케줄링 알고리즘에 따라 실행 요청 정렬 후, 하나씩 꺼냄 / lock을 이용하여 ready queue에 대한 동기화된 접근 보장
	# lock 이미 선언되어있음 (self.lock)
    def schedule_requests(self):
        # Schedule requests based on the specified algorithm
        if len(self.ready_queue) == 0:
            return None
        self.lock.acquire()
        if self.algorithm == "FIFO":    # request time
            self.ready_queue = sorted(self.ready_queue, key = lambda x : x[5])  
        elif self.algorithm == "RM":    # period
            self.ready_queue = sorted(self.ready_queue, key = lambda x : x[3])  
        elif self.algorithm == "EDF":   # sort by absolute deadline
            self.ready_queue = sorted(self.ready_queue, key = lambda x : x[4])  
        if len(self.ready_queue) > 0:
            first_element = self.ready_queue.pop(0)
        else :
            first_element = None
        # print(f"[Scheduler] first element : {first_element}")
        self.lock.release()
        return first_element

    def execute_request(self, request):
        # Execute the scheduled request
        # This function should perform the following tasks:
        # 1. Get the interpreter of the requested model.
        # 2. Process the input image.
        # 3. Perform Edge TPU computation.
        # 4. Find the class with the highest probability and return it to the client.
        try:
            client_id, model_name, image_file, period, deadline, request_time = request
            print(f"[Scheduler] Executing request : {request}")
        except Exception as e:
            print(f"Error parsing request : {e}")
            return
        
        # Get interpreter from dictionary
        interpreter = self.interpreters[model_name]
        
        size = common.input_size(interpreter1)
        image = Image.open(image_file).convert('RGB').resize(size, Image.LANCZOS)
        
        common.set_input(interpreter, image)
        # Run inference
        interpreter.invoke()
        
        classes = classify.get_classes(interpreter)
        if classes:
            classes = sorted(classes, key=lambda x : x.score, reverse = True)
            result = f"{self.labels[classes[0].id]},{classes[0].score}"
        else :
            result = f"Nolabel, 0.00000"
        
        print(f"[Scheduler] Sending response : {result}")
        client_pipe = Pipe(f"/tmp/pipe_{client_id}", create = False)
        client_pipe.write(result)
        # client_pipe.flush()
        
        return

    def run(self):
        # Run the scheduler
        # 1. Start the thread to receive requests.
        # 2. Periodically check, schedule, and execute requests.
        listen_thread = threading.Thread(target = self.receive_request_loop)
        listen_thread.start()
        while True:
            try:
                time.sleep(1e-6)
                # print(f"length : {len(self.ready_queue)}")
                if len(self.ready_queue) > 0:
                    request = self.schedule_requests()
                    print(f"[Scheduler] Scheduled request: {request}")
                    if request != None:
                        self.execute_request(request)
            except Exception as e:
                print(f"Error reading from pipe: {e}")
        return


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Scheduler")
    parser.add_argument(
        "--algorithm", type=str, required=True, help="Scheduling algorithm"
    )

    args = parser.parse_args()
    scheduler = Scheduler(args.algorithm)
    scheduler.run()
