/**
 * Centralized toast notification utilities
 * Provides consistent success/error messaging patterns
 */

import { toaster } from "@decky/api";
import t from "../i18n/i18n";

export interface ToastOptions {
  title: string;
  body: string;
}

/**
 * Show a success toast notification
 */
export function showSuccessToast(title: string, body: string): void {
  toaster.toast({
    title,
    body
  });
}

/**
 * Show an error toast notification
 */
export function showErrorToast(title: string, body: string): void {
  toaster.toast({
    title,
    body
  });
}

/**
 * Standard success messages for common operations
 */
export const ToastMessages = {
  get INSTALL_SUCCESS() {
    return {
      title: t("TOAST_INSTALL_COMPLETE", "Installation Complete"),
      body: t("TOAST_INSTALL_COMPLETE_DESC", "MAKO Renderer developer build installed privately")
    };
  },
  get INSTALL_ERROR() {
    return {
      title: t("TOAST_INSTALL_FAILED", "Installation Failed"),
      body: t("TOAST_UNKNOWN_ERROR", "Unknown error occurred")
    };
  },
  get UNINSTALL_SUCCESS() {
    return {
      title: t("TOAST_UNINSTALL_COMPLETE", "MAKO Renderer Removed"),
      body: t("TOAST_UNINSTALL_COMPLETE_DESC", "MAKO Renderer files have been removed")
    };
  },
  get UNINSTALL_ERROR() {
    return {
      title: t("TOAST_UNINSTALL_FAILED", "Uninstallation Failed"),
      body: t("TOAST_UNKNOWN_ERROR", "Unknown error occurred")
    };
  },
  get CONFIG_UPDATE_ERROR() {
    return {
      title: t("TOAST_CONFIG_UPDATE_FAILED", "Update Failed"),
      body: t("TOAST_CONFIG_UPDATE_FAILED_DESC", "Failed to update configuration")
    };
  },
  get CLIPBOARD_SUCCESS() {
    return {
      title: t("TOAST_CLIPBOARD_SUCCESS", "Copied to Clipboard!"),
      body: t("TOAST_CLIPBOARD_SUCCESS_DESC", "Launch option ready to paste")
    };
  },
  get CLIPBOARD_ERROR() {
    return {
      title: t("TOAST_CLIPBOARD_FAILED", "Copy Failed"),
      body: t("TOAST_CLIPBOARD_FAILED_DESC", "Unable to copy to clipboard")
    };
  }
};

/**
 * Show a toast with dynamic error message
 */
export function showErrorToastWithMessage(title: string, error: unknown): void {
  const errorMessage = error instanceof Error ? error.message : String(error);
  showErrorToast(title, errorMessage);
}

/**
 * Show installation success toast
 */
export function showInstallSuccessToast(): void {
  showSuccessToast(ToastMessages.INSTALL_SUCCESS.title, ToastMessages.INSTALL_SUCCESS.body);
}

/**
 * Show installation error toast
 */
export function showInstallErrorToast(error?: string): void {
  showErrorToast(ToastMessages.INSTALL_ERROR.title, error || ToastMessages.INSTALL_ERROR.body);
}

/**
 * Show uninstallation success toast
 */
export function showUninstallSuccessToast(): void {
  showSuccessToast(ToastMessages.UNINSTALL_SUCCESS.title, ToastMessages.UNINSTALL_SUCCESS.body);
}

/**
 * Show uninstallation error toast
 */
export function showUninstallErrorToast(error?: string): void {
  showErrorToast(ToastMessages.UNINSTALL_ERROR.title, error || ToastMessages.UNINSTALL_ERROR.body);
}

/**
 * Show clipboard success toast
 */
export function showClipboardSuccessToast(): void {
  showSuccessToast(ToastMessages.CLIPBOARD_SUCCESS.title, ToastMessages.CLIPBOARD_SUCCESS.body);
}

/**
 * Show clipboard error toast
 */
export function showClipboardErrorToast(): void {
  showErrorToast(ToastMessages.CLIPBOARD_ERROR.title, ToastMessages.CLIPBOARD_ERROR.body);
}
