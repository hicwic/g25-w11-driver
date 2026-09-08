// SPDX-License-Identifier: GPL-2.0-only
namespace G25GfnWheelBridge;

/// <summary>
/// Parsed options for the <c>dry-run</c> and <c>bridge</c> commands.
/// </summary>
sealed record BridgeOptions
{
    // The USB/IP profile (full USB descriptor, bcdDevice 0x8900) is the one
    // GeForce NOW recognises. The plain "logitech-g29" HIDMaestro profile uses
    // the UMDF backend and enumerates as 046D:C24F:0100, which GFN rejects with
    // "No known device with interface number 0".
    public const string DefaultProfile = "logitech-g29-usbip";

    public string Profile { get; set; } = DefaultProfile;
    public string? ProfilesDirectory { get; set; }
    public int RateHz { get; set; } = 250;
    public double DurationSeconds { get; set; }
    public bool InstallDriver { get; set; }
    public bool ForwardButtons { get; set; } = true;
    public bool ForwardHat { get; set; } = true;
    public bool InvertAccelerator { get; set; }
    public bool InvertBrake { get; set; }
    public bool InvertClutch { get; set; }
    public bool TraceOutput { get; set; }
    public bool RelayForceFeedback { get; set; } = true;
    public bool KeepExisting { get; set; }
    // Sent to the G25 at startup so it is usable without G HUB. 0 = don't set range.
    public int WheelRangeDegrees { get; set; } = 900;
}
