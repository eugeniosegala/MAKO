import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

const { toast, status, request, listeners, removeEventListener } = vi.hoisted(
  () => ({
    toast: vi.fn(),
    status: vi.fn(),
    request: vi.fn(),
    listeners: new Map<string, () => void>(),
    removeEventListener: vi.fn(),
  }),
);
vi.mock("@decky/api", () => ({
  toaster: { toast },
  addEventListener: (event: string, listener: () => void) => {
    listeners.set(event, listener);
    return listener;
  },
  removeEventListener,
}));
vi.mock("../../src/api/makoApi", () => ({
  getFrameRecordingStatus: status,
  requestFrameRecording: request,
}));

import {
  FRAME_RECORDING_SHORTCUT_EVENT,
  registerShortcutRecording,
} from "../../src/utils/frameRecording";

const ringGame = {
  process: "ds.exe",
  mode: "ring" as const,
  seconds: 10,
  delay: 5,
};
const saved = {
  success: true,
  status: "complete x frames=140 dropped=0",
  recordings: [
    {
      name: "x",
      complete: true,
      frames: 140,
      dropped: 0,
      width: 2560,
      height: 1440,
    },
  ],
};
const bodies = () => toast.mock.calls.map(([{ body }]) => body);

describe("frame recordings", () => {
  const pressSticks = () => listeners.get(FRAME_RECORDING_SHORTCUT_EVENT)?.();

  beforeEach(() => {
    vi.useFakeTimers();
    listeners.clear();
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.clearAllMocks();
  });

  test("pressing both sticks saves a rolling recording and reports it", async () => {
    status
      .mockResolvedValueOnce({ success: true, armed: [ringGame] })
      .mockResolvedValueOnce({ success: true, pending: true })
      .mockResolvedValueOnce({ success: true, status: "saving x" })
      .mockResolvedValueOnce(saved);
    request.mockResolvedValue({ success: true });
    const stop = registerShortcutRecording();
    pressSticks();
    await vi.runAllTimersAsync();
    expect(request).toHaveBeenCalledTimes(1);
    expect(bodies()).toEqual([
      "Saved 140 frames (2560x1440, 0 dropped): x",
    ]);
    const listener = listeners.get(FRAME_RECORDING_SHORTCUT_EVENT);
    stop();
    expect(removeEventListener).toHaveBeenCalledWith(
      FRAME_RECORDING_SHORTCUT_EVENT,
      listener,
    );
  });

  test("a burst announces its delay before reporting the recording", async () => {
    status
      .mockResolvedValueOnce({
        success: true,
        armed: [{ ...ringGame, mode: "burst", seconds: 3 }],
      })
      .mockResolvedValueOnce({ success: true, status: "scheduled now" })
      .mockResolvedValueOnce({ success: true, status: "scheduled now" })
      .mockResolvedValueOnce({ success: true, status: "recording x" })
      .mockResolvedValueOnce(saved);
    request.mockResolvedValue({ success: true });
    registerShortcutRecording();
    pressSticks();
    await vi.runAllTimersAsync();
    expect(bodies()).toEqual([
      "Recording 3 s in 5 s; frame generation pauses until it ends.",
      "Saved 140 frames (2560x1440, 0 dropped): x",
    ]);
  });

  test("every other outcome still ends in a notification", async () => {
    request.mockResolvedValue({ success: false, error: "not armed" });
    status.mockResolvedValue({ success: true, armed: [] });
    registerShortcutRecording();
    pressSticks();
    await vi.runAllTimersAsync();

    request.mockResolvedValue({ success: true });
    status.mockResolvedValue({
      success: true,
      status: "refused the game's swapchain was created without copy access",
    });
    pressSticks();
    await vi.runAllTimersAsync();

    status.mockResolvedValue({ success: true, pending: true });
    pressSticks();
    await vi.runAllTimersAsync();

    status.mockResolvedValue({ success: true, status: "saving x" });
    pressSticks();
    await vi.runAllTimersAsync();

    expect(bodies()).toEqual([
      "not armed",
      "Recording refused: the game's swapchain was created without copy access",
      "The game did not take the request; it records only while it shows frames.",
      "No result after 180 s; the recording may still be saving.",
    ]);
  });

  test("a press while one is followed is acknowledged, not repeated", async () => {
    status
      .mockResolvedValueOnce({ success: true, armed: [ringGame] })
      .mockResolvedValueOnce(saved);
    request.mockResolvedValue({ success: true });
    registerShortcutRecording();
    pressSticks();
    pressSticks();
    await vi.runAllTimersAsync();
    expect(request).toHaveBeenCalledTimes(1);
    expect(bodies()).toEqual([
      "Still handling the previous L3 + R3 press.",
      "Saved 140 frames (2560x1440, 0 dropped): x",
    ]);
  });
});
