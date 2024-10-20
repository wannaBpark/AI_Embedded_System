import argparse


def evaluate(log_file):
    total_requests = 0
    missed_deadlines = 0

    with open(log_file, "r") as f:
        for line in f:
            result,_,__,___ = line.strip().split(",")
            total_requests += 1
            if "fail" in result:
                missed_deadlines += 1

    deadline_miss_rate = missed_deadlines / total_requests
    print(f"log file ({log_file}) evaluation result:")
    print(f"  Total Requests: {total_requests}")
    print(f"  Missed Deadlines: {missed_deadlines}")
    print(f"  Deadline Miss Rate: {deadline_miss_rate:.2f}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Evaluate client logs")
    parser.add_argument("--log_file", type=str, help="Client ID")

    args = parser.parse_args()
    evaluate(args.log_file)
