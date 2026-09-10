import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Gootar — NAM Librarian",
  description:
    "Organise, tag, audition and A/B a library of Neural Amp Modeler captures.",
};

export default function RootLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
