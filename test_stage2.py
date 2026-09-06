import subprocess
import time
import os

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

# 1. Test Custom Resolution
send_cmd("render res 1024 640")
time.sleep(2.0)
send_cmd("screenshot stage2_custom_res_1024x640.png --ui")
time.sleep(1.5)

# 2. Test Roughness and Normal Textures on Cornell Box materials
send_cmd("render res 1280 720")
send_cmd("material set 5 roughness_tex resource/textures/striped_roughness.png 1.0") # short box
send_cmd("material set 5 roughness 0.8")
send_cmd("material set 6 normal_tex resource/textures/bump_normal.png 2.0")       # tall box
send_cmd("material set 6 roughness 0.2")
send_cmd("material set 6 metallic 0.7")
send_cmd("render reset")
time.sleep(3.0)
send_cmd("screenshot stage2_textures_cornell_ui.png --ui")
time.sleep(1.0)
send_cmd("screenshot stage2_textures_cornell_view.png")
time.sleep(1.5)

# 3. Test Nishita 1993 Sky Model (Daytime & Sunset)
send_cmd("sky mode nishita")
send_cmd("sky sun 35 135 20")
send_cmd("cam pos 0.0 1.0 -4.0")
send_cmd("cam target 0.0 6.0 6.0") # look up into sky
send_cmd("render reset")
time.sleep(3.0)
send_cmd("screenshot stage2_nishita_sky_day_ui.png --ui")
time.sleep(1.0)
send_cmd("screenshot stage2_nishita_sky_day_view.png")
time.sleep(1.5)

# Sunset looking directly towards sun near horizon
send_cmd("sky sun 4 180 35")
send_cmd("sky turbidity 3.0")
send_cmd("cam pos 0.0 1.0 -4.0")
send_cmd("cam target 0.0 1.5 10.0") # looking towards azimuth 180 and low elevation 4
send_cmd("render reset")
time.sleep(3.0)
send_cmd("screenshot stage2_nishita_sky_sunset_ui.png --ui")
time.sleep(1.0)
send_cmd("screenshot stage2_nishita_sky_sunset_view.png")
time.sleep(1.5)

send_cmd("quit")
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

print("Stage 2 test finished successfully.")
