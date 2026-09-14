// Authored line icons, one consistent stroke weight (1.6) and viewBox (0 0 20
// 20), no fill except where the glyph itself is a solid shape (Play). Kept
// deliberately small in number -- this sandbox needs a handful of controls,
// not an icon system.
import type { SVGProps } from "react";

function Base(props: SVGProps<SVGSVGElement>) {
  return (
    <svg
      width="18"
      height="18"
      viewBox="0 0 20 20"
      fill="none"
      stroke="currentColor"
      strokeWidth={1.6}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      {...props}
    />
  );
}

export function SunIcon(props: SVGProps<SVGSVGElement>) {
  return (
    <Base {...props}>
      <circle cx="10" cy="10" r="3.6" />
      <path d="M10 2v2M10 16v2M2 10h2M16 10h2M4.6 4.6l1.4 1.4M14 14l1.4 1.4M4.6 15.4L6 14M14 6l1.4-1.4" />
    </Base>
  );
}

export function MoonIcon(props: SVGProps<SVGSVGElement>) {
  return (
    <Base {...props}>
      <path d="M14.8 12.2A6.4 6.4 0 1 1 7.8 5.2a5.1 5.1 0 0 0 7 7Z" />
    </Base>
  );
}

export function StepIcon(props: SVGProps<SVGSVGElement>) {
  return (
    <Base {...props}>
      <path d="M5 4.5v11l7-5.5-7-5.5Z" />
      <path d="M14.5 4.5v11" />
    </Base>
  );
}

export function PlayIcon(props: SVGProps<SVGSVGElement>) {
  return (
    <Base {...props} fill="currentColor">
      <path d="M6 4.2v11.6c0 .7.8 1.1 1.4.7l9-5.8a.8.8 0 0 0 0-1.4l-9-5.8c-.6-.4-1.4 0-1.4.7Z" />
    </Base>
  );
}

export function PauseIcon(props: SVGProps<SVGSVGElement>) {
  return (
    <Base {...props} fill="currentColor" stroke="none">
      <rect x="5.5" y="4" width="3" height="12" rx="1" />
      <rect x="11.5" y="4" width="3" height="12" rx="1" />
    </Base>
  );
}

export function RunIcon(props: SVGProps<SVGSVGElement>) {
  return (
    <Base {...props}>
      <path d="M4 10h9M9.5 5.5 14 10l-4.5 4.5" />
      <path d="M15.6 5.5v9" />
    </Base>
  );
}
