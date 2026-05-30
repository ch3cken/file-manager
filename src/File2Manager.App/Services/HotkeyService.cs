using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Windows.Input;
using System.Windows.Interop;

namespace File2Manager.App.Services;

public sealed class HotkeyService : IDisposable
{
    private const int HotkeyId = 0x4632;
    private const int WmHotkey = 0x0312;
    private const uint ModAlt = 0x0001;
    private const uint ModControl = 0x0002;
    private const uint ModShift = 0x0004;
    private const uint ModWin = 0x0008;
    private const uint ModNoRepeat = 0x4000;

    private readonly IntPtr _windowHandle;
    private readonly HwndSource _source;
    private Action? _callback;
    private bool _registered;

    public HotkeyService(IntPtr windowHandle)
    {
        _windowHandle = windowHandle;
        _source = HwndSource.FromHwnd(windowHandle) ?? throw new InvalidOperationException("Window handle is unavailable.");
        _source.AddHook(HandleWindowMessage);
    }

    public void Register(string gesture, Action callback)
    {
        Unregister();
        var (modifiers, virtualKey) = ParseGesture(gesture);

        if (!RegisterHotKey(_windowHandle, HotkeyId, modifiers | ModNoRepeat, virtualKey))
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Unable to register hotkey " + gesture + ".");
        }

        _callback = callback;
        _registered = true;
    }

    public void Unregister()
    {
        if (!_registered)
        {
            return;
        }

        UnregisterHotKey(_windowHandle, HotkeyId);
        _registered = false;
    }

    private IntPtr HandleWindowMessage(IntPtr hwnd, int message, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (message == WmHotkey && wParam.ToInt32() == HotkeyId)
        {
            _callback?.Invoke();
            handled = true;
        }

        return IntPtr.Zero;
    }

    private static (uint Modifiers, uint VirtualKey) ParseGesture(string gesture)
    {
        if (string.IsNullOrWhiteSpace(gesture))
        {
            gesture = "Alt+Space";
        }

        var parts = gesture.Split('+', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (parts.Length == 0)
        {
            throw new InvalidOperationException("Hotkey is empty.");
        }

        var modifiers = 0u;
        Key? key = null;

        foreach (var part in parts)
        {
            if (part.Equals("Alt", StringComparison.OrdinalIgnoreCase))
            {
                modifiers |= ModAlt;
            }
            else if (part.Equals("Ctrl", StringComparison.OrdinalIgnoreCase) ||
                     part.Equals("Control", StringComparison.OrdinalIgnoreCase))
            {
                modifiers |= ModControl;
            }
            else if (part.Equals("Shift", StringComparison.OrdinalIgnoreCase))
            {
                modifiers |= ModShift;
            }
            else if (part.Equals("Win", StringComparison.OrdinalIgnoreCase) ||
                     part.Equals("Windows", StringComparison.OrdinalIgnoreCase))
            {
                modifiers |= ModWin;
            }
            else
            {
                key = ParseKey(part);
            }
        }

        if (key is null)
        {
            throw new InvalidOperationException("Hotkey must include a key.");
        }

        return (modifiers, (uint)KeyInterop.VirtualKeyFromKey(key.Value));
    }

    private static Key ParseKey(string value)
    {
        if (value.Equals("Space", StringComparison.OrdinalIgnoreCase))
        {
            return Key.Space;
        }

        var converter = new KeyConverter();
        var converted = converter.ConvertFromInvariantString(value);
        if (converted is Key key)
        {
            return key;
        }

        throw new InvalidOperationException("Unsupported hotkey key: " + value);
    }

    public void Dispose()
    {
        Unregister();
        _source.RemoveHook(HandleWindowMessage);
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool UnregisterHotKey(IntPtr hWnd, int id);
}
