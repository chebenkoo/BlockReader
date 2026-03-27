# MicromineWrapper / DbText.dll — Exploration Notes

## Goal
Replace the hand-rolled binary parser in `MicromineReader.cpp` (which produces corrupted/spiked geometry) with a reliable reader that uses the same SDK the C# codebase uses.

---

## Discovery Commands

### 1 — Find the DLL in the repo
```powershell
Get-ChildItem -Path "d:\spry\spry" -Recurse -Filter "MicromineWrapper*" -ErrorAction SilentlyContinue | Select-Object FullName, Length
Get-ChildItem -Path "d:\spry\spry" -Recurse -Filter "Micromine*.dll"    -ErrorAction SilentlyContinue | Select-Object FullName, Length
```
**Found:** `QTBlockModelReader\MicromineWrapper.dll` (1,260,672 bytes)

### 2 — Check PE header type (.NET vs native)
```powershell
$f   = [System.IO.File]::ReadAllBytes("...\MicromineWrapper.dll")
$pe  = [System.BitConverter]::ToInt32($f, 60)               # PE offset
$mach= [System.BitConverter]::ToUInt16($f, $pe+4)           # machine type
$opt = [System.BitConverter]::ToUInt16($f, $pe+24)          # optional header magic
# CLR RVA at data directory entry 14 (offset varies by PE32/PE32+)
$clr = [System.BitConverter]::ToUInt32($f, $pe+24+112+14*8)
```
**Result:**
| Field | Value | Meaning |
|---|---|---|
| Machine | `0x8664` | x64 |
| Opt magic | `0x20B` | PE32+ (64-bit) |
| CLR RVA | `0x2FF40` | **Non-zero → .NET assembly** (C++/CLI mixed-mode) |

### 3 — Enumerate .NET types in MicromineWrapper.dll
```powershell
$asm = [System.Reflection.Assembly]::LoadFile("...\MicromineWrapper.dll")
$asm.GetTypes() | ForEach-Object { Write-Output $_.FullName }
```
**Key managed types exposed:**
- `MicromineWrapper.DbTextWrapper` — main entry point: open a `.DAT` file
- `MicromineWrapper.CDbTextStructureWrapper` — field/schema access
- `MicromineWrapper.CDbTextRecordWrapper` — per-record data access

Internal (unmanaged) types leaked into the type list confirm it wraps `DbText::CDbTextRecord` and `DbText::CDbTextStructure` — these are the real native classes.

### 4 — Find the underlying native DLL
```powershell
Get-ChildItem "D:\spry\spry" -Recurse -Filter "DbText*" -ErrorAction SilentlyContinue | Select-Object FullName, Length
```
**Found:**
- `Design.UnitTest\DbText.dll` (878,456 bytes)
- `UI.Design.Test\DbText.dll` (878,456 bytes) — same file

### 5 — Verify DbText.dll is a pure native DLL
```powershell
$f   = [System.IO.File]::ReadAllBytes("...\DbText.dll")
$pe  = [System.BitConverter]::ToInt32($f, 60)
$mach= [System.BitConverter]::ToUInt16($f, $pe+4)    # 0x8664 = x64
$clr = [System.BitConverter]::ToUInt32($f, $pe+24+112+14*8)  # 0x0 = native
```
**Result:** `Machine=0x8664`, `CLR_RVA=0x0` → **pure native, no .NET runtime required**

### 6 — Inspect DbText.dll imports (dependency chain)
```powershell
$obj = "C:\Qt\Tools\mingw1310_64\bin\objdump.exe"
& $obj -p "...\DbText.dll" 2>&1 | Select-String "DLL Name"
```
**DbText.dll needs these DLLs at runtime (all present in `Design.UnitTest\`):**
```
AParser.dll     fieldname.dll    format.dll
mmsystem.dll    UnitSystem.dll   MmBlockModel.dll
TriStore.dll    roaring.dll      icuuc70.dll (+ icu companions)
zlibwapi.dll    MSVCP140.dll     VCRUNTIME140.dll
```

### 7 — List companion DLLs already present
```powershell
Get-ChildItem "D:\spry\spry\common\Design.UnitTest\" -Filter "*.dll" | Select-Object Name
```
All required DLLs confirmed present in `Design.UnitTest\`.

---

## What the C# Code Does (from MicromineDatFileReader.Test.cs)

```csharp
using (var dbText = new DbTextWrapper(filePath))
{
    var structure  = dbText.Structure;
    var fieldCount = structure.GetFieldCount();

    // Field metadata
    var field = structure.GetField(i);
    field.GetName();            // e.g. "EAST", "_NORTH", "AuCut"
    field.GetAttributeType();   // Numeric/Real/Integer/Text/etc.
    field.GetIndex();           // field ID for data access

    // Record iteration
    var recordCount = dbText.GetTotalRecords();
    for (var i = 0; i < recordCount; i++) {
        if (!dbText.LoadRecord(i)) continue;   // skips deleted records automatically
        var x = dbText.GetDouble(xFieldId);
        var y = dbText.GetDouble(yFieldId);
        var z = dbText.GetDouble(zFieldId);
    }
}
```

Key things `DbTextWrapper` handles internally that `MicromineReader.cpp` must guess at:
| Issue | C# (SDK) | C++ (current) |
|---|---|---|
| Record count | `GetTotalRecords()` — exact | stride-hunt heuristic |
| Deleted records | `LoadRecord(i)` returns `false` | manual status-byte check (fragile) |
| Record byte offset | SDK owns layout | guessed 2-byte prefix + 26-byte start gap |
| Field byte offsets | `GetIndex()` + typed getter | manually computed from var list |
| Cross-page alignment | handled internally | caused drift bugs |

---

## Architecture Summary

```
MicromineWrapper.dll  (C++/CLI, mixed-mode .NET)
        │ wraps
        ▼
DbText.dll  (pure native x64, MSVC ABI)
        │ depends on
        ▼
AParser.dll  fieldname.dll  format.dll  mmsystem.dll
UnitSystem.dll  MmBlockModel.dll  TriStore.dll  icu*.dll
```

**Why we cannot call DbText.dll directly from MinGW C++:**
DbText.dll exports C++ class methods with MSVC name mangling. MinGW uses a GCC ABI — incompatible. There are also no public headers in the repo.

---

## Chosen Plan: Path 2 — C# Reader Subprocess

Build a small standalone C# console app (`MicromineProxy`) that:
1. Takes a `.DAT` file path as a command-line argument
2. Uses `DbTextWrapper` to read all records correctly
3. Writes a compact binary stream to `stdout`:
   ```
   [int32: record_count]
   [int32: field_count]
   [field_count × (int32 name_len + UTF8 name bytes)]
   [record_count × field_count × double64]
   ```
4. Exits cleanly

The Qt app (`main.cpp`) launches it with `QProcess`, reads stdout into memory, and populates `BlockModelSoA` without any binary format guessing.

---

## Tasks

- [ ] **1. Create `MicromineProxy` C# project**
  - Target: .NET 8 (self-contained, win-x64)
  - Reference: `MicromineWrapper.dll` + all companion DLLs
  - Output: single `MicromineProxy.exe`

- [ ] **2. Implement `MicromineProxy/Program.cs`**
  - Args: `<path.DAT> [field1 field2 ...]` (optional field filter)
  - Read fields via `structure.GetFieldCount()` / `GetField(i).GetName()`
  - Iterate records with `LoadRecord(i)` + `GetDouble(id)`
  - Write binary protocol to stdout (little-endian)

- [ ] **3. Add `MicromineSubprocessReader` to C++ side**
  - New file: `src/BlockModel/MicromineSubprocessReader.cpp`
  - Uses `QProcess` to launch `MicromineProxy.exe`
  - Parses binary stdout into `BlockModelSoA`
  - Has same signature as `MicromineReader::load()`

- [ ] **4. Wire into `main.cpp` / `ModelController`**
  - In `preScan()`: call proxy with `--fields-only` flag to get field list
  - In `loadWithMapping()`: call proxy with full read

- [ ] **5. Bundle DLLs**
  - Copy `DbText.dll` + companions next to `MicromineProxy.exe`
  - Add `POST_BUILD` CMake step to copy `MicromineProxy.exe` next to the Qt app

- [ ] **6. Test**
  - `Bush Pig_Complete_Model.DAT` (55 MB) — expect ~150k blocks, Grade range [0.001, 11.5]
  - Verify `BLOCK[0] xyz` matches expected centroid `(10, 480, 65)`

---

## Binary Protocol (stdout from MicromineProxy.exe)

```
Offset  Type        Description
------  --------    -----------
0       int32       magic = 0x4D4D424D ("MMBM")
4       int32       record_count
8       int32       field_count  (N)
12      N × entry   field descriptors:
          int32     name byte length
          bytes     UTF-8 name
?       R×N×8       doubles, row-major [record][field]
```

Error case: if DbTextWrapper throws, write `0x45525221` ("ERR!") to stdout and error message to stderr, exit 1.
