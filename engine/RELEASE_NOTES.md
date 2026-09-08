## What's new in MAKO Renderer v3.2.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/the-captain.png" alt="The Captain: a Renaissance-style pixel-art captain and crew plotting an attack over a nautical chart aboard their ship, while a colossal mako looms over the fleet in the stormy sea behind them" width="100%">

### Release codename: The Captain

> _“A steady hand cannot still the sea. It can keep us on course.”_
>
> **Captain Matteo Veyr, _Council of the Last Fleet_**

---

### Stability, frame pacing, and compatibility

MAKO 3.2 addresses uneven frame delivery and unnecessary recovery resets, updates launcher compatibility, and protects saved settings during failed writes and updates.

- **Stability and frame-pacing fixes:** Adaptive now accounts for slow frames in uneven game cadence, avoiding inflated source-FPS estimates that could suppress generated frames. Isolated brief timeouts retain the established cadence estimate, recovery retries remain pending through frames with no generated output, and renewed demand after a menu can restart generation sooner.
- **VKD3D-Proton compatibility:** Preserves extended presentation timing used by newer VKD3D-Proton clients, fixing a rejection that could stop presentation on the first frame.
- **Ubisoft Connect launcher exclusion:** Ubisoft Connect and its web UI stay outside MAKO activation, while the child game keeps its normal profile matching.
- **Scaling memory accounting:** Budgets now account for where scaling runs relative to Frame Generation and the larger 5x workload. Failed graphics-resource setup also releases exported handles instead of leaking them.
- **Lossless Scaling DLL detection:** Fixes DLL lookup in Steam installations under a custom XDG data directory and adds an availability check for the selected LS1 model and sharpness.
- **Configuration save fixes:** Closing the configuration window flushes pending edits, and failed writes preserve the previous configuration.
- **Installer recovery and Flatpak fixes:** The standalone installer restores the previous native installation if an update fails. The Flatpak installer also fixes terminal runtime selection and exits without installing when cancelled.
