import subprocess
import time

def run_instance():
    return subprocess.Popen(['./test_app'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

# Run primary instance
primary = run_instance()
time.sleep(1)  # Give it time to start

# Try to run 5 more instances
secondaries = [run_instance() for _ in range(5)]

# Wait for all processes to finish
primary.wait()
for sec in secondaries:
    sec.wait()

# Check results
primary_output = primary.stdout.read().decode('utf-8')
assert "Primary instance - running" in primary_output

for i, sec in enumerate(secondaries):
    sec_output = sec.stdout.read().decode('utf-8')
    assert "Secondary instance - exiting" in sec_output, f"Secondary {i} did not exit as expected"

print("All tests passed successfully!")