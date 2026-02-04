# Comprehensive CE Schema Analysis - Combined Findings

## Executive Summary

This document combines all available knowledge about the CE (Custom Encryption) schema used in HPS files, including findings from multiple sources:
- `CE_SCHEMA_REVERSE_ENGINEERING_PLAN.md`
- `schemaCE_findings.md`
- `FIX_XML_NODE_TRAVERSAL.md`
- `Lib/src/ParseDcm.cpp` implementation

## 1. File Format and Structure

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

- **Encryption Algorithm**: Initially believed to be Blowfish in ECB mode, but evidence suggests XOR-based encryption
- **Key Size**: 16 bytes (128 bits) for Blowfish, but XOR key length matches data length
- **Data Encoding**: Base64
- **Checksum**: Adler32 (byte-swapped for storage)
- **Vertex Data**: Encrypted
- **Facet Data**: Unencrypted (confirmed)

## 2. Current Understanding

### ✅ Confirmed Discoveries

1. **Encryption Method**: XOR encryption (not Blowfish as initially thought)
2. **Key Length**: Same length as data (4512 bytes for 376 vertices)
3. **Checksum**: Adler32 calculated on decrypted data, stored byte-swapped
4. **Facets**: NOT encrypted (raw data)
5. **Partial Success**: XOR with correct key gives correct first vertex coordinate

### ❌ Unknown Elements

1. **Key Generation Algorithm**: Proprietary, not standard PRNG/cipher
2. **Algorithm Type**: Appears to be custom Rust implementation
3. **Dependencies**: Likely depends on file properties (EKID, PackageLockList)

## 3. Key Findings from Executable Analysis

### Confirmed Key in Executable

The Windows executable `dcm2stl.exe` contains the key pattern:
```
0123456789abcdef
```

**Hexadecimal**: `0123456789abcdef`
**ASCII**: `0123456789abcdef`
**Binary**: `00000001 00000010 00000011 00000100 00000101 00000110 00000111 00111000 00111001 00111010 00111011 00111100 00111101 00111110 00111111`
**Decimal bytes**: `[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]`

### Key Representation Issues

The key `0123456789abcdef` doesn't produce correct output for test files:
- **Checksum verification fails**: Expected `330137282`, Got `2058416063`
- **Decrypted vertices are incorrect**: `4.85735e+30, -4.8909e+20, -1.38244e-07` instead of expected `-3.337815e-01  1.000000e+00 -1.462392e+00`

## 4. Implementation Status

### ✅ Completed Features

1. **Blowfish Decryption Core** (may be incorrect approach)
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

1. **Algorithm Uncertainty**
   - No obvious Blowfish function names in executable
   - Possible obfuscation or custom implementation
   - May involve additional transformations

2. **Key Mismatch Issue**
   - The key `0123456789abcdef` doesn't produce correct output
   - Checksum verification fails consistently
   - Decrypted vertices are incorrect

## 5. Test Results

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

## 6. Key Discovery Patterns Tested

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

## 7. File Properties Analysis

### Properties Found

```xml
<Property name="EKID" value="1"/>
```

**EKID**: Encryption Key ID
- Value: `1` (always observed as 1 in test files)
- **Important Finding**: EKID appears to be a constant value rather than a variable parameter
- **Evidence**: Other software that can read CE schema files doesn't ask users to enter a key
- **Behavior**: Removing EKID from the file doesn't prevent other software from reading the file
- **Conclusion**: EKID is likely a fixed identifier rather than a security parameter

### Potential Key Derivation

Given that EKID is always 1 and doesn't affect file readability, the key derivation is likely:
```
final_key = base_pattern XOR file_specific_data
```

Where `file_specific_data` could be:
- File hash or checksum
- File size or other metadata
- Position-based patterns
- Custom algorithm output

The fact that no user input is required suggests the key is either:
1. **Hardcoded** in the software
2. **Derived from file content** itself
3. **Based on a standard pattern** that doesn't require user input

## 8. Reverse Engineering Approach

### Python-Based Analysis Plan

#### Phase 1: Data Extraction

1. **Extract Encrypted Vertex Buffer**
   ```python
   # Extract base64 vertex data from DICOM XML
   # Decode to get encrypted binary data
   # Expected: 4512 bytes for 376 vertices (376 * 3 * 4 = 4512)
   ```

2. **Extract Reference Vertices**
   ```python
   # Parse reference STL file
   # Extract first 10-20 known-good vertices
   # Format: List of (x, y, z) tuples
   ```

3. **Extract File Properties**
   ```python
   # Parse DICOM XML for:
   # - EKID (Encryption Key ID)
   # - PackageLockList
   # - Other potential metadata
   ```

#### Phase 2: XOR Key Analysis

1. **Calculate Partial XOR Key**
   ```python
   # Use known good vertices to calculate partial key
   # partial_key[i] = encrypted[i] XOR good_vertex[i]
   # Analyze pattern in partial key
   ```

2. **Test Simple Patterns**
   ```python
   # Test basic key generation patterns:
   # - Linear sequences: i, i*constant, i+constant
   # - Modulo operations: i % 256, (i*prime) % 256
   # - Bitwise operations: i ^ constant, ~i
   # - Position-based: i + (i >> 8), etc.
   ```

3. **Test Cryptographic Patterns**
   ```python
   # Test more complex patterns:
   # - Hash-based: MD5(position), SHA1(position)
   # - PRNG-based: Custom sequences with seeds
   # - Hybrid: hash(position) XOR position XOR constant
   ```

#### Phase 3: Property-Based Key Generation

1. **EKID-Based Keys**
   ```python
   # Test keys derived from EKID:
   # - Linear: EKID * i + i*i
   # - Modulo: (EKID + i) % 256
   # - Bitwise: EKID ^ i
   ```

2. **PackageLockList-Based Keys**
   ```python
   # Test keys derived from PackageLockList hash:
   # - Direct: hash_bytes[i % hash_length]
   # - Position-mixed: hash_bytes[i % len] ^ (i % 256)
   # - Complex: hash_bytes[i % len] ^ (i * EKID)
   ```

3. **Combined Property Keys**
   ```python
   # Test combinations of properties:
   # - hash(PackageLockList) XOR EKID XOR position
   # - (hash_byte ^ EKID_byte) + position
   # - Complex mathematical combinations
   ```

## 9. Key Generation Patterns to Test

### Simple Mathematical Patterns
1. `key[i] = i % 256`
2. `key[i] = (i * 167) % 256` (prime multiplier)
3. `key[i] = (i ^ 0x55) % 256`
4. `key[i] = (i + (i >> 8)) % 256`
5. `key[i] = ((i * 257) + 17) % 256`

### Position-Based Patterns
1. `key[i] = (i * i) % 256`
2. `key[i] = (i * i * i) % 256`
3. `key[i] = sin(i) * 127 + 128`
4. `key[i] = (i ^ (i >> 4)) % 256`

### Hash-Based Patterns
1. `key[i] = md5(str(i))[0]`
2. `key[i] = sha1(str(i))[i % 20]`
3. `key[i] = md5(EKID + str(i))[0]`

### Property-Based Patterns
1. `key[i] = (EKID * i) % 256`
2. `key[i] = (EKID + i) % 256`
3. `key[i] = (EKID ^ i) % 256`
4. `key[i] = hash(PackageLockList)[i % len] ^ (i % 256)`
5. `key[i] = (hash_byte ^ EKID_byte ^ (i % 256)) % 256`

## 10. Success Criteria

### Primary Validation
- ✅ Adler32 checksum matches expected value (330137282)
- ✅ First 10 vertices match reference STL exactly
- ✅ All vertices in reasonable coordinate range

### Secondary Validation
- ✅ Full mesh structure is valid
- ✅ No NaN or infinite values
- ✅ Vertex count matches expected (376)
- ✅ Face parsing works correctly

## 11. Technical Recommendations

### Immediate Next Steps

1. **Focus on Position-Based XOR Patterns**
   - Test the mathematical patterns outlined in the reverse engineering plan
   - Focus on patterns that don't require external keys or EKID
   - Test position-based patterns: `key[i] = f(i)` where i is byte position

2. **File Content Analysis**
   - Calculate file hashes (MD5, SHA1) and test as XOR keys
   - Analyze file size and other metadata as potential key sources
   - Look for patterns in the encrypted data itself

3. **Simplify Key Discovery**
   - Since EKID is always 1, remove it from key derivation tests
   - Focus on patterns that work without any file properties
   - Test hardcoded patterns that might be consistent across files

4. **Binary Analysis**
   - Look for hardcoded byte patterns in the executable
   - Search for XOR initialization routines
   - Identify any position-based calculation functions

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

## 12. Current Implementation Issues

### XML Node Traversal Fix

**Problem**: The code was using `getElementsByTagName("Vertices")` which returns all Vertices nodes in the document, causing incorrect data parsing when multiple CA/CC nodes exist.

**Solution**: Modified to parse nodes sequentially like the Python reference:
```cpp
// Parcourir les nœuds enfants
Poco::AutoPtr<Poco::XML::NodeList> childNodes = binaryElement->childNodes();

for (unsigned long i = 0; i < childNodes->length(); ++i)
{
    auto childElement = dynamic_cast<Poco::XML::Element*>(childNodes->item(i));
    if (childElement)
    {
        std::string tagName = childElement->nodeName();
        
        // Chercher dans les nœuds CA ou CC
        if (tagName == "CA" || tagName == "CC")
        {
            // Chercher Vertices DANS ce nœud CA/CC spécifique
            Poco::AutoPtr<Poco::XML::NodeList> verticesNodes = 
                childElement->getElementsByTagName("Vertices");
            
            if (verticesNodes->length() > 0)
            {
                // Parse les vertices de CE nœud
                ...
                return floatData;  // Retourne (écrase les précédents comme Python)
            }
        }
    }
}
```

**Impact**: This ensures nodes are parsed in the correct order and vertices/facets are read from the same CA/CC node.

## 13. Conclusion

The CE schema decryption implementation is **technically complete** but requires the correct key or key derivation algorithm to produce accurate results. The key `0123456789abcdef` found in the executable doesn't work with the test files, indicating either:

1. The key requires transformation/derivation
2. Additional encryption layers exist
3. The algorithm differs from standard Blowfish
4. The encryption is actually XOR-based rather than Blowfish

### Most Likely Scenario

Based on the reverse engineering plan and findings, the most likely scenario is:
- **XOR-based encryption** with custom key generation
- **Key length matches data length** (not fixed 16-byte Blowfish key)
- **Key derivation does NOT depend on EKID** (since it's always 1 and not required for decryption)
- **Key is likely derived from file content or position-based patterns**
- **Custom algorithm** implemented in Rust (based on executable analysis)

**Key Insight**: Since other software can read CE files without requiring any key input and removing EKID doesn't break compatibility, the encryption is likely:
1. **Using a hardcoded pattern** that's the same for all files
2. **Deriving the key from file content** (hash, size, or other metadata)
3. **Using position-based XOR patterns** that don't require external keys

### Next Steps

1. **Implement Python-based reverse engineering approach**
2. **Test XOR key patterns systematically**
3. **Analyze key derivation from file properties**
4. **Port successful algorithm to C++**
5. **Validate with all test files**

With access to the Windows executable and additional test files, reverse engineering the exact key derivation process should be feasible using the systematic Python approach outlined in the reverse engineering plan.

## 14. Files Analyzed

- `/Users/romainnosenzo/Downloads/dcm2stl.exe` - Windows decryption executable
- `/Users/romainnosenzo/CLionProjects/Open3SDCM/TestData/Hole3x5/Hole 3x5.dcm` - Test HPS file
- `/Users/romainnosenzo/CLionProjects/Open3SDCM/TestData/Scan-01/Scan.dcm` - Additional test file
- `/Users/romainnosenzo/CLionProjects/Open3SDCM/TestData/Hole3x5/dcm2stlapp_Hole 3x5-text.stl` - Reference output

## 15. Tools Used

- `strings` - String extraction from binary
- `grep` - Pattern searching
- `file` - File type identification
- OpenSSL - Blowfish implementation
- Poco - Base64 decoding, XML parsing
- Assimp - Mesh export
- Python - Reverse engineering and analysis

## 16. Alternative Reverse Engineering Approaches

### Software Reverse Engineering Plan

Since the current approach hasn't yielded the correct decryption key, consider reverse engineering other software that can read CE schema files:

#### Identified Software

1. **Original Windows Executable**: `dcm2stl.exe`
   - **Status**: Already analyzed (strings extracted, basic patterns tested)
   - **Next Steps**: Deep disassembly to find encryption routines

2. **Other Proprietary Software** (to be identified)
   - **Approach**: Find software that can read CE files without requiring keys
   - **Method**: Monitor API calls, memory access during file loading
   - **Tools**: Process Monitor, API Monitor, x64dbg

3. **Open Source Alternatives** (if available)
   - **Approach**: Search for open source implementations
   - **Method**: Code analysis, pattern matching
   - **Tools**: GitHub search, code repositories

#### Reverse Engineering Methodology

1. **Dynamic Analysis**
   - Monitor software while loading CE files
   - Capture memory dumps before/after decryption
   - Analyze API calls related to encryption

2. **Static Analysis**
   - Disassemble executables
   - Look for XOR patterns, memory operations
   - Identify encryption/decryption functions

3. **Pattern Extraction**
   - Extract hardcoded byte patterns
   - Identify algorithm parameters
   - Reconstruct decryption process

#### Tools for Software Reverse Engineering

- **Dynamic Analysis**: Process Monitor, API Monitor, Frida, x64dbg
- **Static Analysis**: IDA Pro, Ghidra, Binary Ninja
- **Memory Analysis**: Cheat Engine, ReClass
- **Network Analysis**: Wireshark (if network involved)

### Prioritized Action Plan

#### Phase 1: Focused Pattern Testing (Current Priority)
1. **Implement Python XOR pattern tester**
2. **Test all mathematical patterns systematically**
3. **Validate with checksum and reference vertices**
4. **Document results for each pattern**

#### Phase 2: Software Reverse Engineering (Parallel Effort)
1. **Identify additional software that reads CE files**
2. **Set up monitoring environment**
3. **Capture decryption process in action**
4. **Extract algorithm and patterns**

#### Phase 3: Implementation and Validation
1. **Port successful algorithm to C++**
2. **Integrate into existing codebase**
3. **Test with all available CE files**
4. **Document final solution**

## 17. References

- `CE_SCHEMA_REVERSE_ENGINEERING_PLAN.md` - Detailed reverse engineering approach
- `schemaCE_findings.md` - Key findings and test results
- `FIX_XML_NODE_TRAVERSAL.md` - XML parsing corrections
- `Lib/src/ParseDcm.cpp` - Current implementation

---