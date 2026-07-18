# CV181x camera DKMS modules

This is an out-of-tree development module for the Sophgo CV181x camera
subsystem. It stays outside the kernel package while the BSP-derived register
model is validated and converted to upstream Linux interfaces.

The DKMS package builds five modules from one source package:

- `cv181x-camera-common.ko`: shared bus and memory format handling
- `cv181x-csi2.ko`: CSI-2 receiver and D-PHY wrapper
- `cv181x-vi.ko`: CSI bridge and capture DMA support
- `cv181x-isp.ko`: ISP-top support required by bypass capture
- `cv181x-camera.ko`: media graph, asynchronous binding, and VB2 orchestration

Build against a prepared kernel tree:

```sh
make -C /path/to/linux O=/path/to/build M=$PWD \
	MO=/path/to/module-build modules
```

The module exposes a Media Controller graph and a V4L2 capture node backed by
videobuf2 DMA-contiguous buffers. YUV422 capture, repeated stream start/stop,
and recovery after temporary queue starvation have been validated on the
NanoKVM PCIe at 1920x1080.

The current compatibility DT node provides `csi-mac`, `csi-wrap`, `vi`, and
`vip-sys` resources, the ISP interrupt, CSI/VIP clocks and resets, and a CSI-2
graph endpoint. These resources will move to separate CV181x CSI-2, VI, and
ISP nodes after the source-level split is fully validated.

Build the Debian DKMS package with:

```sh
dpkg-buildpackage -us -uc -b
```

Installing the package registers the source with DKMS for RISC-V kernels. It
does not configure any module to load automatically.

CI builds the native Debian source and binary packages, validates the DKMS
payload, and uploads them as workflow artifacts. Pushes to
`master` also publish through `deb-s3` when the repository has the same
`DEB_S3_*`, AWS, and signing variables and secrets as the kernel package
repository.
