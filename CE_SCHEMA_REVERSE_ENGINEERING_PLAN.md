# CE Schema Reverse Engineering Plan

## Overview
This plan outlines the approach to reverse engineer the XOR key generation algorithm for 3Shape CE schema vertex buffer decompression. The goal is to replace the incorrect Blowfish decryption with the proper XOR-based approach.

## Current Understanding

### ✅ Confirmed Discoveries
- **Encryption Method**: XOR encryption (not Blowfish)
- **Key Length**: Same length as data (4512 bytes for 376 vertices)
- **Checksum**: Adler32 calculated on decrypted data, stored byte-swapped
- **Facets**: NOT encrypted (raw data)
- **Partial Success**: XOR with correct key gives correct first vertex coordinate

### ❌ Unknown Elements
- **Key Generation Algorithm**: Proprietary, not standard PRNG/cipher
- **Algorithm Type**: Appears to be custom Rust implementation
- **Dependencies**: Likely depends on file properties (EKID, PackageLockList)

## Python-Based Reverse Engineering Approach

### Phase 1: Data Extraction

#### 1.1 Extract Encrypted Vertex Buffer
```python
# Extract base64 vertex data from DICOM XML
# Decode to get encrypted binary data
# Expected: 4512 bytes for 376 vertices (376 * 3 * 4 = 4512)
```

#### 1.2 Extract Reference Vertices
```python
# Parse reference STL file
# Extract first 10-20 known-good vertices
# Format: List of (x, y, z) tuples
```

#### 1.3 Extract File Properties
```python
# Parse DICOM XML for:
# - EKID (Encryption Key ID)
# - PackageLockList
# - Other potential metadata
```

### Phase 2: XOR Key Analysis

#### 2.1 Calculate Partial XOR Key
```python
# Use known good vertices to calculate partial key
# partial_key[i] = encrypted[i] XOR good_vertex[i]
# Analyze pattern in partial key
```

#### 2.2 Test Simple Patterns
```python
# Test basic key generation patterns:
# - Linear sequences: i, i*constant, i+constant
# - Modulo operations: i % 256, (i*prime) % 256
# - Bitwise operations: i ^ constant, ~i
# - Position-based: i + (i >> 8), etc.
```

#### 2.3 Test Cryptographic Patterns
```python
# Test more complex patterns:
# - Hash-based: MD5(position), SHA1(position)
# - PRNG-based: Custom sequences with seeds
# - Hybrid: hash(position) XOR position XOR constant
```

### Phase 3: Property-Based Key Generation

#### 3.1 EKID-Based Keys
```python
# Test keys derived from EKID:
# - Linear: EKID * i + i*i
# - Modulo: (EKID + i) % 256
# - Bitwise: EKID ^ i
```

#### 3.2 PackageLockList-Based Keys
```python
# Test keys derived from PackageLockList hash:
# - Direct: hash_bytes[i % hash_length]
# - Position-mixed: hash_bytes[i % len] ^ (i % 256)
# - Complex: hash_bytes[i % len] ^ (i * EKID)
```

#### 3.3 Combined Property Keys
```python
# Test combinations of properties:
# - hash(PackageLockList) XOR EKID XOR position
# - (hash_byte ^ EKID_byte) + position
# - Complex mathematical combinations
```

### Phase 4: Validation Framework

#### 4.1 Vertex Reasonableness Check
```python
# Validate decrypted floats:
# - Range: -1000 to 1000 (reasonable 3D coordinates)
# - No NaN/Inf values
# - Consistent with reference STL
```

#### 4.2 Checksum Validation
```python
# Calculate Adler32 checksum:
# - Standard Adler32 algorithm
# - Byte-swap result for comparison
# - Compare with expected: 330137282
```

#### 4.3 Full Vertex Comparison
```python
# Compare all decrypted vertices with reference:
# - First 10-20 vertices exact match
# - Statistical similarity for full dataset
# - Mesh structure validation
```

### Phase 5: Implementation

#### 5.1 Python Prototype
```python
# Create comprehensive testing script
# Implement all key generation patterns
# Automated validation framework
# Progress reporting and result analysis
```

#### 5.2 C++ Implementation
```cpp
// Port successful Python algorithm to C++
// Update ParseDcm.cpp
// Replace Blowfish with XOR decryption
// Fix checksum calculation
```

## Key Generation Patterns to Test

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

## Success Criteria

### Primary Validation
- ✅ Adler32 checksum matches expected value (330137282)
- ✅ First 10 vertices match reference STL exactly
- ✅ All vertices in reasonable coordinate range

### Secondary Validation
- ✅ Full mesh structure is valid
- ✅ No NaN or infinite values
- ✅ Vertex count matches expected (376)
- ✅ Face parsing works correctly

## Expected Timeline

1. **Data Extraction**: 1 hour
2. **Pattern Testing**: 2-4 hours (Python allows rapid iteration)
3. **Validation**: 1 hour
4. **C++ Implementation**: 1-2 hours
5. **Testing**: 1 hour

**Total**: 6-9 hours (vs days with C++-only approach)

## Advantages of Python Approach

1. **Rapid Iteration**: No compilation needed
2. **Easy Debugging**: Interactive analysis of results
3. **Comprehensive Testing**: Can test hundreds of patterns quickly
4. **Visualization**: Easy to plot and analyze key patterns
5. **Portability**: Once algorithm found, easy to port to C++

## Next Steps

1. Create Python script with all testing functions
2. Extract and prepare test data
3. Run comprehensive pattern tests
4. Analyze results to identify correct algorithm
5. Implement in C++ and validate

This systematic approach should quickly identify the correct XOR key generation algorithm and resolve the CE schema decompression issue.