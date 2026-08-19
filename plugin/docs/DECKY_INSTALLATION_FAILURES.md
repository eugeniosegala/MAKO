# MAKO Decky Does Not Install or Load

Use this guide when Decky Loader cannot install the **Mako** plugin, reports an error such as `Installation Failed: [Errno 13] Permission denied`, or finishes the installation but Mako never appears or opens.

This is a Decky installation or loading failure, so it does not require Mako or MAKO Renderer to be running. If Mako appears and opens in Decky Loader, use the [MAKO Decky diagnostics guide](https://github.com/eugeniosegala/MAKO/blob/main/plugin/docs/COLLECT_DIAGNOSTICS.md) instead.

Do not try to bypass a permission error with `sudo`, `chmod -R 777`, or a broad recursive `chown`. It normally means that one exact Decky path is not writable by the logged-in user. Changing unrelated paths can break Decky or other plugins.

## Create a Decky loading report

1. Attempt the Mako installation or open Mako once. Save the exact error text or take a screenshot, then stop retrying so the relevant log lines remain near the end of the Decky log.
2. Switch the Steam Deck or Steam Machine to **Desktop Mode** and open **Konsole**.
3. Paste this complete command and press Enter:

    ```bash
    {
      printf '%s\n' '=== MAKO Decky installation/loading report ==='
      date
      uname -a
      printf '\n%s\n' '=== Decky Loader service ==='
      systemctl status plugin_loader.service --no-pager --full 2>&1 || true
      journalctl -u plugin_loader.service -b --no-pager -n 800 2>&1 || true
      printf '\n%s\n' '=== Mako plugin log ==='
      if [ -f "$HOME/homebrew/logs/Mako/plugin.log" ]; then
        tail -n 1200 "$HOME/homebrew/logs/Mako/plugin.log"
      else
        printf 'Mako plugin log not found: %s\n' "$HOME/homebrew/logs/Mako/plugin.log"
      fi
      printf '\n%s\n' '=== Relevant Decky path permissions ==='
      ls -ld "$HOME/homebrew" "$HOME/homebrew/plugins" \
        "$HOME/homebrew/plugins/Mako" "$HOME/homebrew/logs" \
        "$HOME/homebrew/logs/Mako" 2>&1 || true
    } > "$HOME/Desktop/MAKO-Decky-load-report.txt"
    ```

    Konsole normally shows no report text because the command sends all output to the Desktop file. It is normal for the report to say that the Mako folder or plugin log was not found when Decky could not install or start the plugin.

4. Open `MAKO-Decky-load-report.txt` from the **Desktop** folder and review it before sharing. It can contain usernames, plugin names, filesystem paths, and recent Decky log lines. Remove personal path components you do not want to share, and never send passwords, account credentials, device serial numbers, or licence keys.
5. Submit the report through the [MAKO diagnostic report form][diagnostic-form]. Include the exact error text, whether the failure happened during installation or while opening Mako, and the Mako version or ZIP filename if known.

If you knowingly used `sudo` to install an earlier Mako or Decky file, mention that in the form. Do not guess which path to repair: the report should identify the specific owner, path, and failed operation needed for a narrow fix.

[diagnostic-form]: https://docs.google.com/forms/d/e/1FAIpQLScSd9qgkYCq3Kbbc3_52k4_82iTmEqt3_FxOqGuxQ6FsjutgA/viewform
