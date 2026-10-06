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
import {
  MakoSectionHeader,
  MakoSectionTail,
  makoAccentColor,
  makoPanelDivider,
  makoPanelStyle,
} from "./MakoUi";

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

  const notice = busy ? "" : error || status?.message || "";
  const attention = Boolean(
    notice || status?.conflict || status?.success === false,
  );
  const state = busy
    ? "working"
    : attention
      ? "attention"
      : !status
        ? "checking"
        : status.frame_generation_active
          ? "active"
          : status.running
            ? "streaming"
            : status.installed
              ? "ready"
              : !status.available
                ? "unavailable"
                : "disabled";
  const statusText =
    state === "working"
      ? t("REMOTE_PLAY_WORKING", "Working…")
      : state === "attention"
        ? t("REMOTE_PLAY_ATTENTION", "Needs attention")
        : state === "checking"
          ? t("REMOTE_PLAY_CHECKING", "Checking Remote Play…")
          : state === "active"
            ? t("REMOTE_PLAY_ACTIVE", "Remote Play — frame generation active")
            : state === "streaming"
              ? t(
                  "REMOTE_PLAY_RUNNING",
                  "Remote Play running — frame generation inactive",
                )
              : state === "ready"
                ? t(
                    "REMOTE_PLAY_READY",
                    "Override installed — waiting for a stream",
                  )
                : state === "unavailable"
                  ? t(
                      "REMOTE_PLAY_UNAVAILABLE",
                      "Native Steam streaming client unavailable",
                    )
                  : t("REMOTE_PLAY_OFF", "Remote Play override not installed");
  const indicatorColor =
    state === "attention" || state === "unavailable"
      ? "#f4a259"
      : state === "active"
        ? "#5fe3b1"
        : state === "disabled"
          ? "#738891"
          : makoAccentColor;

  return (
    <>
      <MakoSectionHeader>
        {t("REMOTE_PLAY_TITLE", "Remote Play")}
      </MakoSectionHeader>
      <PanelSectionRow>
        <MakoSectionTail>
          <div
            style={{
              width: "100%",
              display: "grid",
              gap: "12px",
              marginTop: "8px",
            }}
          >
            <div
              className={`Mako_BrandButton${status?.managed ? " Mako_BrandButton--danger" : ""}`}
              style={{ width: "100%" }}
            >
              <ButtonItem
                layout="below"
                bottomSeparator="none"
                description={
                  <MakoInfo
                    className="Mako_OptionDescription"
                    style={{
                      color: "#aebfc5",
                      fontSize: "10px",
                      fontWeight: 400,
                      lineHeight: 1.4,
                    }}
                  >
                    {t(
                      "REMOTE_PLAY_DESCRIPTION",
                      "Uses the Remote Play profile for native Steam streams. Replaces Steam’s streaming client with a wrapper and preserves the original for removal. Close streams before changing it; Steam updates may require reinstalling the override. Steam Link and browser streaming are not supported.",
                    )}
                  </MakoInfo>
                }
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
            <div
              data-mako-remote-play-status={state}
              style={{ ...makoPanelStyle, width: "100%" }}
            >
              <div
                role="status"
                aria-live="polite"
                aria-atomic="true"
                style={{
                  display: "grid",
                  gridTemplateColumns: "8px minmax(0, 1fr)",
                  gap: "8px",
                  padding: "10px 12px",
                  alignItems: "center",
                }}
              >
                <span
                  aria-hidden="true"
                  style={{
                    width: "7px",
                    height: "7px",
                    borderRadius: "50%",
                    background: indicatorColor,
                  }}
                />
                <div style={{ minWidth: 0 }}>
                  <div
                    style={{
                      color: "#839da5",
                      fontSize: "9px",
                      fontWeight: 500,
                      lineHeight: 1.3,
                      marginBottom: "3px",
                    }}
                  >
                    {t("REMOTE_PLAY_STATUS", "Status")}
                  </div>
                  <div
                    style={{
                      color: "#d7e7eb",
                      fontSize: "10px",
                      fontWeight: 500,
                      lineHeight: 1.4,
                      overflowWrap: "anywhere",
                    }}
                  >
                    {statusText}
                  </div>
                </div>
              </div>
              {notice && (
                <div
                  role="alert"
                  style={{
                    borderTop: makoPanelDivider,
                    padding: "8px 12px",
                    color: "#f7d9b4",
                    fontSize: "10px",
                    fontWeight: 400,
                    lineHeight: 1.4,
                    overflowWrap: "anywhere",
                  }}
                >
                  {notice}
                </div>
              )}
            </div>
          </div>
        </MakoSectionTail>
      </PanelSectionRow>
    </>
  );
}
