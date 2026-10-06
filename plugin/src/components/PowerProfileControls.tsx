import { useRef, useState } from "react";
import { DropdownItem, PanelSectionRow, ToggleField } from "@decky/ui";
import { setProfilePowerModes } from "../api/makoApi";
import { showErrorToast } from "../utils/toastUtils";
import t from "../i18n/i18n";
import { MakoInfo } from "./MakoInfo";
import { MakoSectionTail } from "./MakoUi";

interface Props {
  profileName: string;
  enabled: boolean;
  powerMode: string;
  powerSource: string;
  disabled?: boolean;
  flushConfigChanges?: () => Promise<void>;
  loadProfileConfig: (name: string, mode?: string) => Promise<void>;
}

export function PowerProfileControls({
  profileName,
  enabled,
  powerMode,
  powerSource,
  disabled = false,
  flushConfigChanges,
  loadProfileConfig,
}: Props) {
  const [busy, setBusy] = useState(false);
  const currentProfile = useRef(profileName);
  currentProfile.current = profileName;
  const changeEnabled = async (value: boolean) => {
    setBusy(true);
    try {
      if (flushConfigChanges) await flushConfigChanges();
      const result = await setProfilePowerModes(profileName, value);
      if (!result.success) throw new Error(result.error || "Unknown error");
      if (currentProfile.current === profileName)
        await loadProfileConfig(profileName);
    } catch (error) {
      showErrorToast(
        t("PROFILE_UPDATE_CONFIG_FAILED", "Failed to update profile config"),
        String(error),
      );
    } finally {
      setBusy(false);
    }
  };
  return (
    <>
      <PanelSectionRow>
        <ToggleField
          label={t("POWER_SEPARATE", "Separate power settings")}
          checked={enabled}
          disabled={busy || disabled}
          onChange={(value) => void changeEnabled(value)}
        />
      </PanelSectionRow>
      {enabled && (
        <PanelSectionRow>
          <DropdownItem
            label={t("POWER_EDIT_MODE", "Editing settings for")}
            selectedOption={powerMode}
            disabled={busy || disabled}
            rgOptions={[
              { data: "shared", label: t("POWER_SHARED", "Base settings") },
              {
                data: "handheld",
                label: t("POWER_HANDHELD", "Handheld (Battery)"),
              },
              { data: "docked", label: t("POWER_DOCKED", "Docked (AC Power)") },
            ]}
            onChange={(option) =>
              void loadProfileConfig(profileName, String(option.data))
            }
          />
        </PanelSectionRow>
      )}
      {enabled && (
        <MakoInfo as={PanelSectionRow}>
          <MakoSectionTail>
            <div
              style={{
                paddingTop: "8px",
                fontSize: "11px",
                lineHeight: 1.35,
                color: "#b8c5d6",
              }}
            >
              {powerSource === "docked"
                ? t("POWER_CURRENT_AC", "Current power: AC")
                : powerSource === "handheld"
                  ? t("POWER_CURRENT_BATTERY", "Current power: Battery")
                  : t(
                      "POWER_CURRENT_UNKNOWN",
                      "Power source unavailable; Base settings apply at startup.",
                    )}{" "}
              {t(
                "POWER_HELP",
                "Switches automatically with the power source. Some changes require a game restart.",
              )}
            </div>
          </MakoSectionTail>
        </MakoInfo>
      )}
    </>
  );
}
