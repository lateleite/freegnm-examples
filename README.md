# Example freegnm programs

This repository holds programs to show how to use [freegnm](https://github.com/lateleite/freegnm/tree/c): a GPU library for the PlayStation 4, and [psbc](https://github.com/lateleite/psbc): a shader binary compiler also for the PlayStation 4.

These samples are meant to be as simple and straightforward as possible.

**NOTES:**

- None of these examples were tested on a Pro/NEO PS4 and may not work there.

## Examples available

### Triangle

Very basic example of the GPU's capabilities.

![Screenshot](triangle/screenshot.png)

### Primitives

Shows the unique primitive types available in the PS4 hardware.

![Screenshot](primitives/screenshot.png)

### Cube

Draws a 3D textured cube.

![Screenshot](cube/screenshot.png)

### GLTF

Renders a GLTF's model with some basic PBR parameters.

![Screenshot](gltf/screenshot.png)

### Instances

Example of instanced draw calls usage.

![Screenshot](instances/screenshot.jpg)

### Indirect

Shows how to setup and use indirect draws.

![Screenshot](indirect/screenshot.png)

## Building

You need the following tools to build the projects:

- Clang
- glslc from shaderc
- GNU Make
- LLD linker
- [OpenOrbis Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain)
- [psbc](https://github.com/lateleite/psbc)

Then go to one of the example's directory, and run

```bash
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain
make
```

## References

Some of the samples are based on [Vulkan samples from Sascha Willems](https://github.com/SaschaWillems/Vulkan).

Some PBR algorithms are taken from [Moving Frostbite to Physically Based Rendering](https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf).

## Libraries and resources

The following libraries are used:

- [cglm](https://github.com/recp/cglm), MIT license
- [cgltf](https://github.com/jkuhlmann/cgltf), MIT license
- [Nuklear](https://immediate-mode-ui.github.io/Nuklear), Public Domain or MIT license
- [stb_image and stb_truetype](https://github.com/nothings/stb), Public Domain or MIT license

The following resources are used:

- [Stanford dragon](https://graphics.stanford.edu/data/3Dscanrep) by Stanford Computer Graphics Laboratory, non-commercial use only
- [Hack typeface](https://sourcefoundry.org/hack) by SourceFoundry, MIT license
- Screenshots of Dark Knight Rises film's plane scene

The libraries and resources are owned by their respective owners and are not covered by this project's license.

## License

This project is released to the public domain, see [UNLICENSE](UNLICENSE) for more information.
