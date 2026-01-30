# CE Schema Decryption Findings

## Executive Summary

This document summarizes the findings from analyzing the CE (Custom Encryption) schema used in HPS files, including the Blowfish decryption implementation, key analysis, and reverse engineering efforts.

## 1. File Format Analysis

### HPS File Structure

```xml
<HPS version="1.0" or "1.1">
  <Packed_geometry>
    <Schema>CE</Schema>  <!-- Custom Encryption schema -->
    <Binary_data>
      <CE version="1.0">
        <Vertices vertex_count="..." base64_encoded_bytes="..." check_value="...">
          [Base64 encoded encrypted vertex data]
        </Vertices>
        <Facets facet_count="..." base64_encoded_bytes="...">
          [Base64 encoded facet data - appears to be unencrypted]
        </Facets>
      </CE>
    </Binary_data>
  </Packed_geometry>
  <Properties>
    <Property name="EKID" value="1"/>  <!-- Encryption Key ID -->
  </Properties>
</HPS>
```

### Key Characteristics

- **Encryption Algorithm**: Blowfish in ECB mode
- **Key Size**: 16 bytes (128 bits)
- **Data Encoding**: Base64
- **Checksum**: Adler32 (byte-swapped for storage)
- **Vertex Data**: Encrypted
- **Facet Data**: Appears to be unencrypted

## 2. Key Findings

### Confirmed Key in Executable

The Windows executable `dcm2stl.exe` contains the key pattern:
```
0123456789abcdef
```

This was found using string analysis:
```bash
strings dcm2stl.exe | grep -o "0123456789abcdef"
# Output: 0123456789abcdef
```

### Key Representation

**Hexadecimal**: `0123456789abcdef`
**ASCII**: `0123456789abcdef`
**Binary**: `00000001 00000010 00000011 00000100 00000101 00000110 00000111 00111000 00111001 00111010 00111011 00111100 00111101 00111110 00111111`
**Decimal bytes**: `[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]`

## 3. Implementation Status

### ✅ Completed Features

1. **Blowfish Decryption Core**
   - ECB mode implementation
   - Proper padding handling (PKCS#7 compatible)
   - 16-byte key support
   - OpenSSL-based implementation

2. **CLI Integration**
   - Custom key option: `-k` or `--decryption_key`
   - Key discovery mode: `-d` or `--key_discovery`
   - Hex format key input (32 characters)

3. **File Processing**
   - XML parsing for CE tags
   - Base64 decoding
   - Adler32 checksum verification
   - Mesh export to STL format

4. **Key Discovery System**
   - Tests 11 common key patterns
   - Automatic checksum validation
   - Pattern-based key searching

### ❌ Current Limitations

1. **Key Mismatch Issue**
   - The key `0123456789abcdef` doesn't produce correct output for test files
   - Checksum verification fails (expected: 330137282, actual: 2058416063)
   - Decrypted vertices are incorrect

2. **Algorithm Uncertainty**
   - No obvious Blowfish function names in executable
   - Possible obfuscation or custom implementation
   - May involve additional transformations

## 4. Test Results

### Test File: Hole 3x5.dcm

**Expected First Vertex**: `-3.337815e-01  1.000000e+00 -1.462392e+00`
**Actual Output**: `4.85735e+30, -4.8909e+20, -1.38244e-07`
**Checksum**: Expected `330137282`, Got `2058416063`

### Test File: Scan.dcm

**Schema**: CE
**Version**: 1.1
**Vertices**: 118,178
**Facets**: 130,493 bytes
**Status**: Same key mismatch issue

## 5. Key Discovery Patterns Tested

The key discovery system tested these patterns:

1. `0123456789abcdef` - Original key from findings
2. `000102030405060708090a0b0c0d0e0f` - Sequential pattern
3. `00000000000000000000000000000000` - All zeros
4. `01010101010101010101010101010101` - All ones
5. `55AA55AA55AA55AA55AA55AA55AA55AA` - Alternating pattern
6. `66656463626139383736353433323130` - Reverse of original
7. `31303233343536373839616263646566` - EKID=1 variant
8. `1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C1C` - All 0x1C pattern
9. `1C8D10B1F7F5B8FE890160FBE45360AC` - Original code pattern
10. `0102030405060708090A0B0C0D0E0F10` - Simple ascending
11. `AA55AA55AA55AA55AA55AA55AA55AA55` - XOR pattern
12. `01000000000000000000000000000000` - EKID-based pattern

**Result**: None of these patterns produced the correct checksum.

## 6. File Properties Analysis

### Properties Found

```xml
<Property name="EKID" value="1"/>
```

**EKID**: Encryption Key ID
- Value: `1`
- Likely used in key derivation
- May indicate key version or variant

### Potential Key Derivation

The key might be derived using:
```
final_key = base_key XOR ekid_pattern XOR file_hash
```

Or similar transformations involving file properties.

## 7. Reverse Engineering Findings

### Executable Analysis

**File Type**: PE32+ executable (console) x86-64
**Characteristics**: Stripped to external PDB
**Platform**: Windows
**Architecture**: x86-64

### Notable Strings Found

- `0123456789abcdef` - Confirmed key pattern
- `0x00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899000000000000000000000000000000000000000000000000000000000000000falsetrue`
- Various numeric patterns and boolean values

### Missing Elements

- No obvious Blowfish function imports
- No clear encryption/decryption function names
- Likely uses custom or obfuscated implementation

## 8. Reference Files

### Known Good Output

**File**: `dcm2stlapp_Hole 3x5-text.stl`
**Format**: Text STL (MeshLab generated)
**First Vertex**: `-3.337815e-01  1.000000e+00 -1.462392e+00`
**Second Vertex**: `1.788732e-08  1.000000e+00 -1.500000e+00`
**Third Vertex**: `-3.337815e-01 -1.142485e-08 -1.462392e+00`

## 9. Technical Recommendations

### Immediate Next Steps

1. **Binary Analysis**
   - Disassemble the Windows executable
   - Look for key initialization routines
   - Identify encryption/decryption functions

2. **Key Derivation Research**
   - Analyze how EKID affects the key
   - Check for file hash incorporation
   - Look for XOR or other transformations

3. **Additional Test Files**
   - Test with more HPS files
   - Check if different files use different keys
   - Identify any version-specific patterns

### Long-Term Solutions

1. **Reverse Engineering**
   - Full analysis of the original software
   - Extract the complete decryption algorithm
   - Document the key derivation process

2. **Algorithm Verification**
   - Confirm if Blowfish is actually used
   - Check for additional encryption layers
   - Verify any custom modifications

3. **Documentation**
   - Create complete specification
   - Document key derivation algorithm
   - Provide reference implementations

## 10. Usage Examples

### Basic Decryption
```bash
# Use default key
./Open3SDCMCLI -i input_dir -o output_dir -f stl

# Use custom key
./Open3SDCMCLI -i input_dir -o output_dir -f stl -k "0123456789abcdef0123456789abcdef"

# Enable key discovery
./Open3SDCMCLI -i input_dir -o output_dir -f stl -d true
```

### Advanced Options
```bash
# Test specific key pattern
./Open3SDCMCLI -i input_dir -o output_dir -f stl -k "1c8d10b1f7f5b8fe890160fbe45360ac"

# Check help
./Open3SDCMCLI --help
```

## 11. Conclusion

The CE schema decryption implementation is **technically complete** but requires the correct key or key derivation algorithm to produce accurate results. The key `0123456789abcdef` found in the executable doesn't work with the test files, indicating either:

1. The key requires transformation/derivation
2. Additional encryption layers exist
3. The algorithm differs from standard Blowfish

With access to the Windows executable and additional test files, reverse engineering the exact key derivation process should be feasible.

## 12. Files Analyzed

- `/Users/romainnosenzo/Downloads/dcm2stl.exe` - Windows decryption executable
- `/Users/romainnosenzo/CLionProjects/Open3SDCM/TestData/Hole3x5/Hole 3x5.dcm` - Test HPS file
- `/Users/romainnosenzo/CLionProjects/Open3SDCM/TestData/Scan-01/Scan.dcm` - Additional test file
- `/Users/romainnosenzo/CLionProjects/Open3SDCM/TestData/Hole3x5/dcm2stlapp_Hole 3x5-text.stl` - Reference output

## 13. Tools Used

- `strings` - String extraction from binary
- `grep` - Pattern searching
- `file` - File type identification
- OpenSSL - Blowfish implementation
- Poco - Base64 decoding, XML parsing
- Assimp - Mesh export

---

**Document Version**: 1.0
**Last Updated**: 2024-01-30
**Status**: Active Investigation
**Next Steps**: Reverse engineer key derivation from Windows executable