import subprocess
import time

proc = subprocess.Popen(
    ["build/NeuTracingRender.exe"],
    stdin=subprocess.PIPE,
    text=True,
    cwd=r"D:\CppPrograms\NeuTracingRender"
)

def send_cmd(cmd):
    proc.stdin.write(cmd + "\n")
    proc.stdin.flush()

time.sleep(3.0)

send_cmd("material set 4 roughness 0.25")
send_cmd("material set 4 metallic 0.95")
send_cmd("material set 7 roughness 0.15")
send_cmd("material set 7 metallic 0.9")

send_cmd("render target 16")
send_cmd("render reset")
send_cmd("wait-spp 16")
time.sleep(1.0)

# Capture noisy 16 spp
send_cmd("denoise off")
time.sleep(0.5)
send_cmd("screenshot stage4_noisy_16spp.png")
time.sleep(1.0)

# Capture denoised 16 spp with color sigma 0.3
send_cmd("denoise on")
send_cmd("denoise set color 0.3")
time.sleep(0.5)
send_cmd("screenshot stage4_denoised_16spp.png")
time.sleep(1.0)

send_cmd("quit")
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

print("Denoise sigma test completed.")
