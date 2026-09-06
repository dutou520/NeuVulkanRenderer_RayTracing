import subprocess
import time

proc = subprocess.Popen(
    ["build/NeuTracingRender.exe"],
    stdin=subprocess.PIPE,
    text=True,
    cwd=r"D:\CppPrograms\NeuTracingRender"
)

def send_cmd(cmd):
    print(f">> {cmd}")
    proc.stdin.write(cmd + "\n")
    proc.stdin.flush()

time.sleep(3.0)

# Set rough metal parameters to test GGX Microfacet Importance Sampling
# Material 4: Gold Monkey, Material 7: Blue Metal Cone
send_cmd("material set 4 roughness 0.25")
send_cmd("material set 4 metallic 0.95")
send_cmd("material set 7 roughness 0.15")
send_cmd("material set 7 metallic 0.9")

# 1. Capture at 8 SPP with Denoiser OFF (noisy baseline)
send_cmd("denoise off")
send_cmd("render target 8")
send_cmd("render reset")
send_cmd("wait-spp 8")
time.sleep(1.0)
send_cmd("screenshot stage4_noisy_8spp.png")
time.sleep(1.0)

# 2. Turn on Denoiser at 8 SPP (clean comparison)
send_cmd("denoise on")
time.sleep(1.0)
send_cmd("screenshot stage4_denoised_8spp.png")
time.sleep(1.0)

# 3. Capture UI showing Denoiser controls
send_cmd("window focus settings")
time.sleep(1.0)
send_cmd("screenshot stage4_denoiser_ui.png --ui")
time.sleep(1.0)

# 4. Accumulate to 64 SPP with GGX Importance Sampling + MIS
send_cmd("render target 64")
send_cmd("wait-spp 64")
time.sleep(1.5)
send_cmd("screenshot stage4_converged_ggx_mis.png")
time.sleep(1.0)

send_cmd("quit")
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

print("Stage 4 test finished.")
