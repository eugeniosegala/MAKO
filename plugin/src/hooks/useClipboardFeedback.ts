import { useEffect, useState } from "react";
import { CLIPBOARD_SUCCESS_DURATION_MS } from "../config/uiTiming";
import { copyWithVerification } from "../utils/clipboardUtils";
import { showClipboardErrorToast } from "../utils/toastUtils";

export function useClipboardFeedback(getText: () => Promise<string>) {
  const [isLoading, setIsLoading] = useState(false);
  const [showSuccess, setShowSuccess] = useState(false);

  useEffect(() => {
    if (!showSuccess) {
      return undefined;
    }

    const timer = setTimeout(
      () => setShowSuccess(false),
      CLIPBOARD_SUCCESS_DURATION_MS,
    );
    return () => clearTimeout(timer);
  }, [showSuccess]);

  const copyToClipboard = async () => {
    if (isLoading || showSuccess) {
      return;
    }

    setIsLoading(true);
    try {
      const text = await getText();
      const { success, verified } = await copyWithVerification(text);

      if (!success) {
        showClipboardErrorToast();
        return;
      }

      setShowSuccess(true);
      if (!verified) {
        console.log("Copy verification failed but copy likely worked");
      }
    } catch {
      showClipboardErrorToast();
    } finally {
      setIsLoading(false);
    }
  };

  return { isLoading, showSuccess, copyToClipboard };
}
