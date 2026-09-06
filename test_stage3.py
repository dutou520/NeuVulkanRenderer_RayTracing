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

# 1. Select texture in FileBrowser & Focus Material Manager
send_cmd("file select resource/textures/bump_normal.png")
send_cmd("window focus mat")
time.sleep(2.0)
send_cmd("screenshot stage3_mat_manager_active.png --ui")
time.sleep(1.5)

# 2. Focus File Browser to showcase bookmarks & filters
send_cmd("window focus file")
time.sleep(2.0)
send_cmd("screenshot stage3_file_browser_active.png --ui")
time.sleep(1.5)

# 3. Apply normal texture & roughness texture to Cornell Box
send_cmd("material set 6 normal_tex resource/textures/bump_normal.png 2.0")
send_cmd("material set 6 roughness 0.2")
send_cmd("material set 6 metallic 0.7")
send_cmd("render reset")
time.sleep(3.0)
send_cmd("screenshot stage3_rendered_scene.png")
time.sleep(1.5)

send_cmd("quit")
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

print("Stage 3 automated test finished.")
