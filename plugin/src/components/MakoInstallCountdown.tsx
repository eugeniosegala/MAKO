interface MakoInstallCountdownProps {
  durationMs: number;
}

export function MakoInstallCountdown({ durationMs }: MakoInstallCountdownProps) {
  return (
    <svg
      aria-hidden="true"
      width="24"
      height="24"
      viewBox="0 0 24 24"
      style={{ display: "block", transform: "rotate(-90deg)" }}
    >
      <circle
        cx="12"
        cy="12"
        r="9"
        fill="none"
        stroke="rgba(208, 138, 160, 0.24)"
        strokeWidth="2.5"
      />
      <circle
        cx="12"
        cy="12"
        r="9"
        pathLength="1"
        fill="none"
        stroke="#d08aa0"
        strokeWidth="2.5"
        strokeLinecap="round"
        strokeDasharray="1"
        strokeDashoffset="0"
      >
        <animate
          attributeName="stroke-dashoffset"
          from="0"
          to="1"
          dur={`${durationMs}ms`}
          fill="freeze"
        />
      </circle>
    </svg>
  );
}
