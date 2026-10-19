# Control4Free: subset reutilizado

Fuente: https://github.com/MoHadiShibli/Control4Free, commit `5a8f9e3d0e75378644e7f7b2df6176c386a24da9`, versión 1.2.1, GPLv3. Los tres archivos C/H se conservan sin modificar; LICENSE contiene la licencia original. El autor atribuye el trabajo de dispositivo virtual a seregonwar/SplashDown (`psbutton.c`, GPL-3.0).

Se reutilizan declaraciones de ABI y el analizador de eventos klog. El adaptador en `src/platform/ps4/virtual_pad.c` es una adaptación para una entrada local, siguiendo la secuencia investigada en main.c/vda.c/web.c de Control4Free. No incorpora servidor web, frontend ni transporte de red. El mapeo y la gestión de tiempo/vida del mando son propios de DualShockToPS4.
