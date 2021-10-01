# Added ELF Sections

## `.aimg_pre_tar`

preloader tar structure

When starting the program,
- unpack the `preloader tar` to a temporary directory
- then read run `start` executable file in that directory 

### Environments

- `ARGV0` : argv[0]
- `APPIMAGE` : app image absolute path
- `APPIMAGE_PREDIR` : preloader unpacked directory path

## `.aimg_sqfs`

application squashfs image
