# Control4Free: reused subset

Source: https://github.com/MoHadiShibli/Control4Free, commit `5a8f9e3d0e75378644e7f7b2df6176c386a24da9`, version 1.2.1, GPLv3. The three C/header files are retained unchanged; LICENSE contains the original license. The author credits the virtual device work to seregonwar/SplashDown (`psbutton.c`, GPL-3.0).

The ABI declarations and klog event parser are reused. The adapter in `src/platform/ps4/virtual_pad.c` supports local input, following the sequence studied in Control4Free's main.c/vda.c/web.c. It does not include the web server, frontend, or network transport. Controller mapping, timing, and lifecycle management are implemented by this project.
