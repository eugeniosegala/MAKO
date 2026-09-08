## What's new in MAKO Renderer v3.2.1

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/the-captain.png" alt="The Captain: a Renaissance-style pixel-art captain and crew plotting an attack over a nautical chart aboard their ship, while a colossal mako looms over the fleet in the stormy sea behind them" width="100%">

### Release codename: The Captain

> _“A steady hand cannot still the sea. It can keep us on course.”_
>
> **Captain Matteo Veyr, _Council of the Last Fleet_**

---

### Game-start compatibility hotfix

MAKO 3.2.1 restores the native Renderer build setup used by the earlier 3.2 tester package, with the same runtime code.

- **Presentation compatibility:** Restores newer Vulkan presentation-metadata support, including `VkPresentId2KHR`, that was omitted from the public 3.2.0 native build when it was compiled with older Vulkan headers.
