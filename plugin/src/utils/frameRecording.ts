import { addEventListener, removeEventListener, toaster } from "@decky/api";
import {
  getFrameRecordingStatus,
  requestFrameRecording,
  type FrameRecordingStatusResult,
} from "../api/makoApi";
import t from "../i18n/i18n";

// A request is followed once a second. The game must take it within 30 s, when
// the Renderer would refuse it as stale, and finish within three minutes.
const FOLLOW_MS = 1000;
const PICKUP_LIMIT = 30;
const FOLLOW_LIMIT = 180;

/** The Renderer's final answer to the latest request, or "" while it has none. */
export function frameRecordingResult(
  status: FrameRecordingStatusResult,
): string {
  const line = status.status ?? "";
  const latest = status.recordings?.[0];
  if (line.startsWith("refused"))
    return t("FRAME_RECORDING_REFUSED", "Recording refused: {reason}", {
      reason: line.slice(8),
    });
  if (line.startsWith("complete") && latest?.complete)
    return t(
      "FRAME_RECORDING_SAVED",
      "Saved {frames} frames ({size}, {dropped} dropped): {name}",
      {
        frames: latest.frames ?? 0,
        size: `${latest.width}x${latest.height}`,
        dropped: latest.dropped ?? 0,
        name: latest.name,
      },
    );
  return "";
}

/** Backend event: both sticks went down together in a game armed for recording. */
export const FRAME_RECORDING_SHORTCUT_EVENT = "frame_recording_shortcut";

const notify = (body: string) =>
  toaster.toast({
    title: t("FRAME_RECORDING_TOAST_TITLE", "MAKO test frames"),
    body,
  });

/** Request one recording and follow it until the Renderer answers. */
async function recordOnce(): Promise<string> {
  const game = (await getFrameRecordingStatus()).armed?.[0];
  const request = await requestFrameRecording();
  if (!request.success) return request.error ?? "";
  let announced = false;
  for (let polls = 1; polls <= FOLLOW_LIMIT; polls++) {
    await new Promise((resolve) => setTimeout(resolve, FOLLOW_MS));
    const status = await getFrameRecordingStatus();
    if (!status.success) return status.error ?? "";
    if (status.pending) {
      if (polls < PICKUP_LIMIT) continue;
      return t(
        "FRAME_RECORDING_NOT_TAKEN",
        "The game did not take the request; it records only while it shows frames.",
      );
    }
    const line = status.status ?? "";
    if (line.startsWith("scheduled") && !announced && game) {
      announced = true;
      notify(
        t(
          "FRAME_RECORDING_SCHEDULED",
          "Recording {seconds} s in {delay} s; frame generation pauses until it ends.",
          { seconds: game.seconds, delay: game.delay },
        ),
      );
    }
    const result = frameRecordingResult(status);
    if (result) return result;
  }
  return t(
    "FRAME_RECORDING_TIMEOUT",
    "No result after {seconds} s; the recording may still be saving.",
    { seconds: (FOLLOW_LIMIT * FOLLOW_MS) / 1000 },
  );
}

/**
 * Pressing both sticks (L3 + R3) in an armed game is the only way to record:
 * a rolling recording saves the moments just seen, a burst starts after its
 * delay. Every press ends in a notification. Returns the unregister function.
 */
export function registerShortcutRecording(): () => void {
  let recording = false;
  const record = async () => {
    if (recording) {
      notify(
        t(
          "FRAME_RECORDING_BUSY",
          "Still handling the previous L3 + R3 press.",
        ),
      );
      return;
    }
    recording = true;
    try {
      notify(await recordOnce());
    } catch (error) {
      console.error("MAKO Decky: shortcut recording failed", error);
      notify(String(error));
    } finally {
      recording = false;
    }
  };
  const listener = addEventListener(FRAME_RECORDING_SHORTCUT_EVENT, () => {
    void record();
  });
  return () => removeEventListener(FRAME_RECORDING_SHORTCUT_EVENT, listener);
}
