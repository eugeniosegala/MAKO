import { useEffect, useRef, useState } from "react";
import { ButtonItem, PanelSectionRow } from "@decky/ui";
import {
  getRemotePlayStatus,
  installRemotePlayOverride,
  removeRemotePlayOverride,
  type RemotePlayResult,
} from "../api/makoApi";
import t from "../i18n/i18n";
import { MakoInfo } from "./MakoInfo";
import { MakoSectionHeader } from "./MakoUi";

interface RemotePlaySectionProps {
  onPrepare?: () => Promise<void>;
  onChanged?: () => void;
}

export function RemotePlaySection({
  onPrepare,
  onChanged,
}: RemotePlaySectionProps) {
  const [status, setStatus] = useState<RemotePlayResult>();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const busyRef = useRef(false);
  const generation = useRef(0);
  const mounted = useRef(true);

  useEffect(() => {
    mounted.current = true;
    let inFlight = false;
    const poll = async () => {
      if (inFlight || busyRef.current) return;
      inFlight = true;
      const request = ++generation.current;
      try {
        const result = await getRemotePlayStatus();
        if (
          mounted.current &&
          request === generation.current &&
          !busyRef.current
        ) {
          setStatus(result);
          setError(result.error || "");
        }
      } catch (failure) {
        if (mounted.current && request === generation.current)
          setError(String(failure));
      } finally {
        inFlight = false;
      }
    };
    void poll();
    const timer = setInterval(() => void poll(), 2000);
    return () => {
      mounted.current = false;
      ++generation.current;
      clearInterval(timer);
    };
  }, []);

  const toggle = async () => {
    if (busyRef.current || !status) return;
    busyRef.current = true;
    ++generation.current;
    setBusy(true);
    setError("");
    try {
      await onPrepare?.();
      const result = await (status.managed
        ? removeRemotePlayOverride()
        : installRemotePlayOverride());
      if (mounted.current) {
        setStatus(result);
        setError(result.error || "");
        if (result.success) onChanged?.();
      }
    } catch (failure) {
      if (mounted.current) setError(String(failure));
    } finally {
      busyRef.current = false;
      if (mounted.current) setBusy(false);
    }
  };

  return (
    <>
      <MakoSectionHeader>
        {t("REMOTE_PLAY_TITLE", "Remote Play")}
      </MakoSectionHeader>
      <PanelSectionRow>
        <div
          className={`Mako_BrandButton${status?.managed ? " Mako_BrandButton--danger" : ""}`}
        >
          <ButtonItem
            layout="below"
            onClick={() => void toggle()}
            disabled={
              !status?.success ||
              busy ||
              status.running ||
              (!status.managed && (!status.available || status.conflict))
            }
          >
            {busy
              ? t("REMOTE_PLAY_WORKING", "Working…")
              : status?.managed
                ? t("REMOTE_PLAY_REMOVE", "Remove Remote Play Override")
                : t("REMOTE_PLAY_INSTALL", "Override Remote Play")}
          </ButtonItem>
        </div>
        <MakoInfo className="Mako_OptionDescription">
          {t(
            "REMOTE_PLAY_DESCRIPTION",
            "Uses the Remote Play profile for native Steam streams. Replaces Steam’s streaming client with a wrapper and preserves the original for removal. Close streams before changing it; Steam updates may require reinstalling the override. Steam Link and browser streaming are not supported.",
          )}
        </MakoInfo>
      </PanelSectionRow>
      <PanelSectionRow>
        {status?.frame_generation_active
          ? t("REMOTE_PLAY_ACTIVE", "Remote Play — frame generation active")
          : status?.running
            ? t(
                "REMOTE_PLAY_RUNNING",
                "Remote Play running — frame generation inactive",
              )
            : status?.installed
              ? t(
                  "REMOTE_PLAY_READY",
                  "Override installed — waiting for a stream",
                )
              : status && !status.available
                ? t(
                    "REMOTE_PLAY_UNAVAILABLE",
                    "Native Steam streaming client unavailable",
                  )
                : t("REMOTE_PLAY_OFF", "Remote Play override not installed")}
        {(error || status?.message) && (
          <div role="alert">{error || status?.message}</div>
        )}
      </PanelSectionRow>
    </>
  );
}
