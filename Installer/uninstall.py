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
import stat
import shutil
import subprocess
def remove_readonly(func, path, _):
	os.chmod(path, stat.S_IWRITE)
	func(path)
def delete_directory(dir):
	if os.path.isdir(dir):
		try:
			shutil.rmtree(dir, onerror=remove_readonly)
			return True
		except:
			return False
	return True

app_name = ".Rendepth"
home_dir = os.path.expanduser("~")
app_dir = os.path.join(home_dir, app_name)

if delete_directory(app_dir):
	print("Removed DepthGenerate Successfully.")
else:
	print("Error Removing DepthGenerate.")