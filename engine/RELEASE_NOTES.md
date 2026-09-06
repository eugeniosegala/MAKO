## What's new in MAKO Renderer v3.2.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/the-captain.png" alt="The Captain: a Renaissance-style pixel-art captain and crew plotting an attack over a nautical chart aboard their ship, while a colossal mako looms over the fleet in the stormy sea behind them" width="100%">

### Release codename: The Captain

> _“A steady hand cannot still the sea. It can keep us on course.”_
>
> **Captain Matteo Veyr, _Council of the Last Fleet_**

---

### A steadier course through troubled waters

The Captain is MAKO 3.2: a focused update to Frame Generation recovery, launcher compatibility, and the safeguards that protect your settings and installation.

- **Steadier Frame Generation recovery:** Brief generated-frame timeouts avoid unnecessary full recovery, pending retries survive frames with no generated output, and Adaptive keeps slow samples from uneven game cadence instead of overestimating the source FPS.
- **Newer Proton compatibility:** Preserves extended presentation timing used by newer VKD3D-Proton clients, fixing a rejection that could stop presentation on the first frame.
- **Cleaner Ubisoft Connect launches:** Ubisoft Connect and its web UI stay outside MAKO activation, while the child game keeps its normal profile matching.
- **More accurate scaling memory limits:** Budgets now account for where scaling runs relative to Frame Generation and the larger 5x workload. Failed graphics-resource setup also releases exported handles instead of leaking them.
- **More reliable Lossless Scaling discovery:** Fixes DLL lookup in Steam installations under a custom XDG data directory and adds an availability check for the selected LS1 model and sharpness.
- **Safer desktop configuration saves:** Closing the configuration window flushes pending edits, and failed writes preserve the previous configuration.
- **Safer installation and updates:** The standalone installer restores the previous native installation if an update fails. The Flatpak installer also fixes terminal runtime selection and handles cancellation cleanly.
