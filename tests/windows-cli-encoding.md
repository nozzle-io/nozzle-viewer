# Windows CLI Encoding

The application uses the CRT `wmain` entry point on Windows and converts each
UTF-16 argument to UTF-8 before the existing parser. Conversion uses
`WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS)` with checked sizing and
conversion calls, owned storage, and a null-terminated argv array. Invalid
UTF-16 is rejected, not replaced. No process locale or ANSI code page is changed.
Non-Windows entry behavior and core name limits are unchanged. MSVC uses its
normal console CRT startup; MinGW targets select Unicode startup with `-municode`.

CI launches separate sender and receiver `nozzle-viewer.exe` processes with Japanese and supplementary Unicode source names and requires exact connected identity plus the existing smoke pixel checks. Unpaired UTF-16 surrogates must fail before parsing. Backend/setup failures fail this check rather than becoming skips.

Run the command in the Windows Unicode CLI step of `.github/workflows/ci.yml`
against that job's built executable. These checks do not establish Unicode
filesystem-path support or interactive console font/code-page behavior.

Contract: [Microsoft WideCharToMultiByte documentation](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte).
