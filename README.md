# Black hole visualization

From this folder , install the build dependencies once:

```bash
sudo dnf install gcc-c++ pkgconf-pkg-config glfw-devel glew-devel glm-devel
```

Build and launch:

```bash
g++ -std=c++17 -O2 -x c++ BlackHolesun.c++ -o blackhole $(pkg-config --cflags --libs glfw3 glew) -lGL
./blackhole
```

Run `./blackhole` from this folder so it can find `BlackBall.comp`,
`grid.vert`, and `grid.frag`. The program needs an OpenGL 4.3 or newer
desktop context. Drag with the left mouse button to orbit, scroll to zoom,
and hold the right mouse button or press G to toggle gravity.
