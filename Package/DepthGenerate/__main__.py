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
import gc
import sys
import zmq

options = {
    "model" : "",
    "depth" : "",
    "upscale": "",
    "maxsize": "",
    "endpoint" : "",
    "mode" : "",
    "base" : "",
    "home" : "",
    "input" : ""
}

print("Starting DepthGenerate")

arg_len = len(sys.argv)
for i in range(1, arg_len):
    if sys.argv[i][:2] == "--":
        for key, value in options.items():
            if sys.argv[i][2:] == key:
                new_val = sys.argv[i + 1]
                options[key] = new_val

try:
    depth_model = int(options["model"])
except ValueError:
    print ("Invalid Depth Model. Default to 0.")
    depth_model = 0
if depth_model < 0 or depth_model > 2:
    print ("Valid Depth Model 0 to 2. Default to 0.")
    depth_model = 0

try:
    depth_size = int(options["depth"])
except ValueError:
    print ("Invalid Depth Size. Default to 560.")
    depth_size = 560

try:
    upscale_size = int(options["upscale"])
except ValueError:
    print ("Invalid Upscale Size. Default to 1920.")
    upscale_size = 1920

try:
    max_size = int(options["maxsize"])
except ValueError:
    print ("Invalid Max Texture Size. Default to 4096.")
    max_size = 4096
if max_size < 4096 or max_size > 16384:
    print ("Valid Max Texture Size 4096 to 16384. Default to 4096.")
    max_size = 4096

try:
    app_mode = int(options["mode"])
except ValueError:
    app_mode = 0
if app_mode < 0 or app_mode > 2:
    app_mode = 0

base_dir = options["base"]
home_dir = options["home"]
input_name = options["input"]

if not input_name:
    print("Invalid Input. Usage '--input Image.jpg'.")
    sys.exit()
if os.name == "nt":
    base_dir = os.path.join(base_dir, '')
    home_dir = os.path.join(home_dir, '')

temp_dir = os.path.join(home_dir, "Temp")
pack_dir = os.path.join(home_dir, "Package")
models_dir = os.path.join(home_dir, "Models")
binary_dir = os.path.join(base_dir, "Binary")
export_name = "3D Export"
export_dir = ""

stereo_tags = ["_anaglyph", "_rgbd", "_sbs_half_width", "_sbs",
               "_free_view", "_qs", "_half_2x1", "_2x1"]

cubevi_tags = ["_cv"]
def append_export_path(dir):
    last_dir = os.path.basename(os.path.normpath(dir))
    if (last_dir != export_name):
        dir = os.path.join(dir, export_name)
    return dir

if os.path.isfile(input_name):
    if app_mode != 2:
        app_mode = 0
    export_dir = os.path.dirname(input_name)
    export_dir = append_export_path(export_dir)

elif os.path.isdir(input_name):
    if app_mode != 2:
        app_mode = 1
    export_dir = input_name
    export_dir = append_export_path(export_dir)

send_io = options["endpoint"]
if app_mode == 2 and send_io:
    signal_context = zmq.Context()
    signal_control = signal_context.socket(zmq.REP)
    signal_control.connect(send_io)
    if not signal_control:
        sys.exit()
    else:
        if not temp_dir:
            sys.exit()

        if not os.path.isdir(temp_dir):
            sys.exit()

        export_dir = temp_dir

if app_mode == 0 and not os.path.isfile(input_name):
    print("Invalid File:", input_name)
    sys.exit()

if app_mode == 1 and not os.path.isdir(input_name):
    print("Invalid Directory:", input_name)
    sys.exit()

if app_mode == 1 and os.path.basename(os.path.normpath(input_name)) == export_name:
    print("Invalid Directory:", input_name)
    sys.exit()

if (app_mode == 0 or app_mode == 1) and export_dir and not os.path.isdir(export_dir):
    os.makedirs(export_dir, exist_ok=True)

import cv2
import torch
import numpy
import pathlib
import importlib
from PIL import Image, ImageFile, ImageFilter
ImageFile.LOAD_TRUNCATED_IMAGES = True
from torchsr.models import ninasr_b0
from torchvision.transforms.functional import to_pil_image, to_tensor
from moviepy import ImageClip

DepthAnyModule = importlib.import_module("Depth-Anything-V2.depth_anything_v2.dpt")
DepthAnythingV2 = DepthAnyModule.DepthAnythingV2

encoders = [ "vits", "vitb", "vitl" ]
labels = [ "Small", "Base", "Large" ]
device_labels = { "cuda" : "GPU Accelerated", "xpu" : "GPU Accelerated", "mps" : "GPU Accelerated", "cpu": "CPU Fallback"}

model_configs = {
    'vits': { 'encoder': 'vits', 'features': 64, 'out_channels': [48, 96, 192, 384]},
    'vitb': {'encoder': 'vitb', 'features': 128, 'out_channels': [96, 192, 384, 768]},
    'vitl': {'encoder': 'vitl', 'features': 256, 'out_channels': [256, 512, 1024, 1024]},
}

depth_models = {
    'vits': 'depth_anything_v2_vits.pth',
    'vitb': 'depth_anything_v2_vitb.pth',
    'vitl': 'depth_anything_v2_vitl.pth',
}

depth_models_url = {
    'vits': 'https://huggingface.co/depth-anything/Depth-Anything-V2-Small/resolve/main/depth_anything_v2_vits.pth?download=true',
    'vitb': 'https://huggingface.co/depth-anything/Depth-Anything-V2-Base/resolve/main/depth_anything_v2_vitb.pth?download=true',
    'vitl': 'https://huggingface.co/depth-anything/Depth-Anything-V2-Large/resolve/main/depth_anything_v2_vitl.pth?download=true',
}

encoder = encoders[depth_model]
label = labels[depth_model]

export_upscale = 3840
screen_aspect = 1.777

print("DepthGenerate Started with", label, "Model - Depth:", depth_size, "- Upscale:", upscale_size,
      "- Max Size:", max_size, "- Service:", app_mode)

DEVICE = 'cuda' if torch.cuda.is_available() else "xpu" if torch.xpu.is_available() else 'mps' if torch.backends.mps.is_available() else 'cpu'
print("Starting DepthGenerate using " + device_labels[DEVICE] + " Mode.")

model = DepthAnythingV2(**model_configs[encoder])
depth_model_path = os.path.normpath(os.path.join(models_dir, depth_models[encoder]))
os.makedirs(models_dir, exist_ok=True)

if os.path.isfile(depth_model_path):
    model.load_state_dict(torch.load(depth_model_path, weights_only=True))
else:
    model.load_state_dict(torch.hub.load_state_dict_from_url(depth_models_url[encoder], model_dir=models_dir,
                                                             weights_only=True))

model = model.to(DEVICE).eval()
nina_models = [ninasr_b0, ninasr_b0, ninasr_b0]

def get_sr_model(s):
    sr_m = nina_models[depth_model](scale=s, pretrained=True)
    sr_m = sr_m.to(DEVICE).eval()
    return sr_m

sr_model = { 2: None, 3: None, 4: None }
for i in range(2, 5):
    sr_model[i] = get_sr_model(i)

print("Depth Model is Fully Loaded.")

torch.cuda.empty_cache()

def sr_upscale(image, scale):
    image_height, image_width = image.shape[:2]
    image_aspect = image_width / image_height
    process_size = max(image_width, image_height)

    image_resize = cv2.resize(image, (process_size, process_size), fx=None, fy=None,
                              interpolation=cv2.INTER_LANCZOS4)

    image_t_c = to_tensor(image_resize)
    image_t = image_t_c.to(DEVICE).unsqueeze(0)

    if (scale == 2 or scale == 3 or scale == 4):
        with torch.no_grad():
            image_sr_t = sr_model[scale](image_t)
    else:
        return image

    image_pil = to_pil_image(image_sr_t.squeeze(0).clamp(0, 1))
    new_image = numpy.array(image_pil)[:, :, ::-1].copy()
    new_image = cv2.cvtColor(new_image, cv2.COLOR_BGR2RGB)

    new_image_height, new_image_width = new_image.shape[:2]
    new_image_aspect = new_image_width / new_image_height

    width_scale = 1.0
    height_scale = 1.0
    if new_image_aspect > image_aspect:
        height_scale = 1.0 / image_aspect
    else:
        width_scale = image_aspect

    new_image = cv2.resize(new_image, (int(image_width * width_scale), int(image_height * height_scale)), fx=None, fy=None,
                           interpolation=cv2.INTER_LANCZOS4)

    del image_resize, image_sr_t, image_t, image_t_c
    gc.collect()
    torch.cuda.empty_cache()

    return new_image

supported_exts = [".jpg", ".jpeg", ".png", ".tga", ".bmp"]

def file_type_supported(file_name):
    file_ext = pathlib.Path(file_name).suffix
    for supported in supported_exts:
        if file_ext.lower() == supported:
            return True
    return False

def file_is_tagged(file_name):
    for tag in stereo_tags:
        if file_name.find(tag) >= 0:
            return True
    return False

def file_is_cubevi(file_name):
    for tag in cubevi_tags:
        if file_name.find(tag) >= 0:
            return True
    return False

def generate_depth(in_file):
    if not in_file:
        print("Exiting. No File to Load.")
        return "ERROR"

    if not os.path.isfile(in_file):
        print("Exiting. Not a File: " + in_file)
        return "ERROR"

    is_supported = file_type_supported(in_file)
    if (not is_supported):
        print("Exiting. Not a Supported Image: " + in_file)
        return "ERROR"

    file_only = os.path.basename(in_file)
    file_wo_ext = os.path.splitext(file_only)[0]
    file_tag = "rgbd"
    file_ext = ".jpg"
    file_sep = "_"
    export_file = file_wo_ext + file_sep + file_tag + file_ext
    out_file = os.path.normpath(os.path.join(export_dir, export_file))

    if file_is_tagged(file_wo_ext):
        if app_mode == 0:
            print("Exiting. File Already 3D Tagged: " + in_file)
            return "ERROR"

    print("Attempting to Load:", in_file)

    if file_is_cubevi(file_wo_ext):
        export_file = file_wo_ext + ".mp4"
        out_file = os.path.normpath(os.path.join(export_dir, export_file))
        print("Try to save MP4:", out_file)
        image_clip = ImageClip(in_file).with_duration(1)
        image_clip.write_videofile(out_file, codec="libx264", fps=24, preset="ultrafast")
        image_clip.close()
        del image_clip
        return out_file

    try:
        image_color = Image.open(in_file).convert('RGB')
        image_color = numpy.array(image_color)[:, :, ::-1].copy()
    except:
        print("Error Loading Image File: " + in_file)
        return "ERROR"

    image_height, image_width = image_color.shape[:2]
    image_aspect = image_width / image_height

    width_restore = 1.0
    height_restore = 1.0

    if (image_aspect > screen_aspect):
        height_restore = 1.0 / image_aspect
        scale_factor = upscale_size / image_width
        process_size = export_upscale
    else:
        width_restore = image_aspect
        scale_factor = (upscale_size / screen_aspect) / image_height
        process_size = int(export_upscale / screen_aspect)

    if process_size > max_size / 2:
        process_size = max_size / 2

    sr_scale_factor = int(scale_factor)
    sr_scale_factor = min(int(sr_scale_factor), 4)

    if (sr_scale_factor > 1):
        image_color = sr_upscale(image_color, sr_scale_factor)

    image_resize = cv2.resize(image_color, (process_size, process_size), fx=None, fy=None,
                              interpolation=cv2.INTER_LANCZOS4)
    image_height, image_width = image_resize.shape[:2]

    with torch.no_grad():
        depth = model.infer_image(image_resize, depth_size)
    depth = (depth - depth.min()) / (depth.max() - depth.min()) * 255.0
    depth = depth.astype(numpy.uint8)
    depth = numpy.repeat(depth[..., numpy.newaxis], 3, axis=-1)

    image_resize = cv2.resize(image_color, (int(image_width * width_restore),
                                            int(image_height * height_restore)), interpolation = cv2.INTER_LANCZOS4)
    depth_resize = cv2.resize(depth, (int(image_width * width_restore),
                                      int(image_height * height_restore)), interpolation = cv2.INTER_LANCZOS4)

    image_output = cv2.hconcat([image_resize, depth_resize])
    success = cv2.imwrite(out_file, image_output, [cv2.IMWRITE_JPEG_QUALITY, 90])

    if success:
        print("Generated Depth Successfully for " + out_file)
    else:
        print("Error Saving Depth to " + out_file)
        return "ERROR"

    del image_color, image_resize, depth, depth_resize, image_output
    gc.collect()
    torch.cuda.empty_cache()

    return out_file

def batch_convert(in_dir):
    if not in_dir:
        print("Exiting. No Directory to Load.")
        return "ERROR"

    if not os.path.isdir(in_dir):
        print("Exiting. Error Loading Directory " + in_dir)
        return "ERROR"

    input_names = os.listdir(in_dir)
    for file in input_names:
        file_wo_ext = os.path.splitext(file)[0]
        if file_is_tagged(file_wo_ext):
            print("Skipping Conversion. File Already 3D Tagged: " + file)
            continue
        generate_depth(os.path.normpath(os.path.join(in_dir, file)))

    print("Generated Depth for Directory Successfully.")

def wait_for_command():
    result = ""
    while (result != "quit"):
        message = signal_control.recv_string()

        if (message == "quit"):
            print("Quiting DepthGenerate")
            signal_control.disconnect(send_io)
            signal_context.destroy()
            sys.exit()

        result = generate_depth(message)
        signal_control.send_string(result)

def main():
    if app_mode == 0:
        generate_depth(input_name)
    elif app_mode == 1:
        batch_convert(input_name)
    elif app_mode == 2:
        wait_for_command()

if __name__ == "__main__":
    main()