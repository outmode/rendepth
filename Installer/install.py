# Copyright (c) 2025 Outmode
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import os
import sys
import stat
import shutil
import subprocess
import venv
def make_directory(path):
	if not os.path.isdir(path):
		try:
			os.makedirs(path, exist_ok=True)
		except OSError:
			print("\nError Creating Directory:", path)
			sys.exit(1)

def remove_readonly(func, path, _):
	os.chmod(path, stat.S_IWRITE)
	func(path)
def delete_directory(dir):
	if os.path.isdir(dir):
		try:
			shutil.rmtree(dir, onerror=remove_readonly)
			return True
		except Exception:
			return False
	return True

def create_environment(env):
	if not os.path.isdir(env):
		try:
			os.makedirs(env, exist_ok=True)
		except OSError:
			print("\nError Creating Directory:", env)
			sys.exit(1)
		try:
			venv.create(env, with_pip=True)
			if os.name == "nt":
				subprocess.check_call([os.path.join(env, "Scripts", "activate.bat")])
		except Exception:
			print("\nError Creating Environment:", env)
			sys.exit(1)

def install_requirements(env, txt):
	print("")
	if os.path.isdir(env):
		req_file = os.path.join("Requirements", txt)
		req_file = os.path.abspath(req_file)
		bin_dir = "bin"
		pip_exe = "pip"
		if os.name == "nt":
			bin_dir = "Scripts"
			pip_exe = "pip"
		pip_exe = os.path.join(env, bin_dir, pip_exe)
		pip_exe = os.path.abspath(pip_exe)
		try:
			subprocess.run([pip_exe, "install", "-r", req_file], cwd=env)
		except Exception:
			print("\nError Installing Requirements:", env)
			sys.exit(1)

def copy_package(app, pkg):
	current_dir = os.path.dirname(os.path.abspath(__file__))
	parent_dir = os.path.dirname(current_dir)
	src_dir = os.path.join(parent_dir, pkg)
	dst_path = os.path.join(app, pkg)
	if os.path.isdir(dst_path):
		try:
			shutil.rmtree(dst_path, onerror=remove_readonly)
		except:
			print("\nError Removing Package.")
			sys.exit(1)
	shutil.copytree(src_dir, dst_path)

def command_choice(option):
	question = option["question"]
	count = 1
	for choice in option["choices"]:
		question += "\n[" + str(count) + "] " + choice
		count = count + 1
	print("\n" + question + "\n")
	user_choice = input(option["prompt"] + ": ")
	count = 1
	for choice in option["choices"]:
		if (str(user_choice) == str(count) or str(user_choice).lower() == choice.lower()):
			return count - 1
		count = count + 1
	print("\n")
	return command_choice(option)


app_name = ".Rendepth"
env_name = "Environment"
pkg_name = "Package"
model_name = "Models"
home_dir = os.path.expanduser("~")
app_dir = os.path.join(home_dir, app_name)
model_dir = os.path.join(app_dir, model_name)
env_path = os.path.join(app_dir, env_name)

cli_menu = {
	"options": [
		{ "question": "Please Choose Your OS",
			"prompt": "Operating System",
			"choices": [ "Windows", "macOS", "Linux"] },
		{ "question": "Please Choose Your Dedicated GPU",
			"prompt": "Graphics Card",
			"choices": [ "Nvidia GeForce", "AMD Radeon", "Intel Arc", "Apple Silicon",
						 "DirectML (Generic GPU)", "No Acceleration (CPU Only)"] }
	]
}

requirements_list = [
	[ 	"nvidia_geforce.txt",
		"amd_windows.txt",
		"intel_arc.txt",
 		"direct_ml.txt",
		"direct_ml.txt",
		"cpu_only.txt" ],
	[ 	"apple_silicon.txt",
		"apple_silicon.txt",
		"apple_silicon.txt",
		"apple_silicon.txt",
		"apple_silicon.txt",
		"cpu_only.txt" ],
	[ 	"nvidia_geforce.txt",
		"amd_linux.txt",
		"intel_arc.txt",
		"cpu_only.txt",
		"cpu_only.txt",
		"cpu_only.txt" ]
]

user_os = command_choice(cli_menu["options"][0])
user_gpu = command_choice(cli_menu["options"][1])
make_directory(app_dir)
make_directory(model_dir)
copy_package(app_dir, pkg_name)
delete_directory(env_path)
create_environment(env_path)
install_requirements(env_path, requirements_list[user_os][user_gpu])