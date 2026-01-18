# MAVAI Tool - Advanced MyKey/SRIX4K NFC Tool

## Overview

MAVAI (MyKey Advanced Vendor Authenticator Interface) is a comprehensive NFC tool for SRIX4K MyKey card management. It extends the functionality of the SRIX Tool with advanced MyKey features, including encryption key calculation, credit management, vendor operations, and WebGUI integration.

## Version

**MAVAI Tool v1.0** - Based on SRIX Tool (PR #1983) with complete MyKey functionality

## Features

### Core SRIX4K Functions
- **Full Tag Reading**: Read complete 128 blocks (512 bytes)
- **Tag Writing**: Write complete dump to tag
- **Clone Tag**: Duplicate tags with all data
- **UID Reading**: Read 8-byte unique identifier
- **File Management**: Save/Load .mavai dump files
- **PN532 Module Info**: Display module information

### MyKey Analysis Functions

#### Encryption Key Calculation
Calculates the MyKey encryption key using the formula:
```
SK = UID × Vendor × OTP (lower 32 bits of result)
```
Where:
- **UID**: First 4 bytes of 8-byte UID in little-endian format
- **Vendor**: 32-bit vendor code extracted from upper 16 bits of decoded blocks 0x18/0x19
- **OTP**: One-Time Programmable value from block 0x06 (byte-swapped, two's complement)

**Note**: Vendor is stored as (vendor - 1) and must be decoded before use in calculation.

#### Block Encode/Decode
XOR-based bit swapping operations for MyKey block obfuscation:
```cpp
encodeDecodeBlock(uint32_t *block)
```
This cryptographic function provides data obfuscation for vendor codes and credit information.

#### Credit Management
- **getCurrentCredit()**: Read current credit from block 0x21 (XOR decrypted)
  - Extracts upper 16 bits: `credit = (decodedBlock >> 16) & 0xFFFF`
  - Lower 16 bits contain date stamp (days since 1/1/1995)
- **addCents()**: Add credits with transaction history logging (blocks 0x34-0x3C)
- **setCents()**: Set absolute credit value
- Display credit in EUR format (cents / 100)

#### Vendor Operations
- **exportVendor()**: Extract vendor ID from blocks 0x18/0x19
  - Decodes blocks and extracts upper 16 bits from each
  - Concatenates: `vendor = ((block18_upper16 << 16) | block19_upper16) + 1`
- **importVendor()**: Write new vendor ID with proper checksum recalculation
  - Stores vendor-1 in upper 16 bits of blocks
  - Calculates checksums with correct block numbers
- Automatic vendor blocks mirroring to 0x1C/0x1D (backup) with separate checksums

#### Key Information Display
- **Key ID**: From block 0x07
- **Production Date**: From block 0x08 (BCD format)
- **Lock ID**: Check from block 0x05 (0x7F indicates locked)
- **Reset Status**: Detection via blocks 0x18/0x19 reset values
- **Days Elapsed**: Since production date calculation

#### Checksum Calculation
```cpp
checksum = 0xFF - blockNum - (sum of nibbles)
```
Stored in the lowest byte of most blocks.

#### Reset Key Function
Factory reset MyKey to default state:
- Reset vendor blocks to 0x8FCD0F48, 0xC0820007
- Clear credit blocks
- Clear transaction history
- Reset transaction pointer

## Data Display Layout

When a tag is read, MAVAI displays:

```
=== MAVAI Tool v1.0 ===
UID: D0 02 XX XX XX XX XX XX
Key ID: XXXXXXXX
-----------------------
Credit: €XX.XX
Vendor: XXXXXXXX
Enc Key: XXXXXXXX
-----------------------
Production: DD/MM/YYYY
Lock ID: [OK/LOCKED]
Status: [ACTIVE/RESET]
```

## Menu Structure

```
MAVAI Tool
├── Main Menu
├── Read Tag
│   └── Full Dump (128 blocks)
├── Read UID
├── Load Dump (.mavai file)
├── Save Dump (from read tag)
├── --- MyKey Tools --- (when dump loaded)
│   ├── View Credit
│   ├── Add Credit (*)
│   ├── Set Credit (*)
│   ├── View Vendor
│   ├── Import Vendor
│   ├── Export Vendor
│   ├── Reset Key (*)
│   └── MyKey Info
├── Clone Tag (write to tag)
└── PN532 Info

(*) = requires confirmation dialog
```

## File Format (.mavai)

MAVAI uses a human-readable text format for dumps:

```
Filetype: Bruce MAVAI Dump
Version: 1.0
UID: D002XXXXXXXXXXXX
KeyID: XXXXXXXX
Vendor: XXXXXXXX
EncryptionKey: XXXXXXXX
Credit: XXXX
ProductionDate: DD/MM/YYYY
Blocks: 128
# Data:
[00] XXXXXXXX
[01] XXXXXXXX
...
[7F] XXXXXXXX
```

Files are stored in: `/BruceRFID/MAVAI/`

## WebGUI Integration

### API Endpoints

MAVAI provides the following REST API endpoints (require authentication):

#### GET /api/mavai/info
Returns MAVAI API status and version.

**Response:**
```json
{
  "status": "MAVAI API available",
  "version": "1.0"
}
```

#### GET /api/mavai/read
Reads tag data (requires hardware access).

**Response:**
```json
{
  "status": "error",
  "message": "Tag reading requires physical hardware access"
}
```

#### GET /api/mavai/credit
Retrieves current credit information.

**Response:**
```json
{
  "credit": 0,
  "currency": "EUR"
}
```

#### POST /api/mavai/credit
Modifies credit (add/set).

**Parameters:**
- `amount`: Credit amount in cents
- `action`: "add" or "set"

**Response:**
```json
{
  "status": "success",
  "action": "add",
  "amount": 500
}
```

#### GET /api/mavai/vendor
Retrieves vendor information.

**Response:**
```json
{
  "vendor": "0x12345678"
}
```

#### POST /api/mavai/vendor
Imports vendor code.

**Parameters:**
- `vendor`: Vendor code in hex (e.g., "12345678")

**Response:**
```json
{
  "status": "success",
  "vendor": "0x12345678"
}
```

#### GET /api/mavai/dump
Downloads current dump as JSON.

**Response:**
```json
{
  "status": "error",
  "message": "No dump loaded"
}
```

#### POST /api/mavai/write
Writes dump to tag (requires hardware access).

**Response:**
```json
{
  "status": "error",
  "message": "Write requires physical hardware access"
}
```

## Constants and Definitions

```cpp
#define SRIX4K_BLOCKS 128
#define SRIX4K_BYTES (SRIX4K_BLOCKS * 4)
#define SRIX_BLOCK_LENGTH 4
#define SRIX_UID_LENGTH 8

// MyKey specific blocks
#define MYKEY_BLOCK_OTP 0x06
#define MYKEY_BLOCK_KEYID 0x07
#define MYKEY_BLOCK_PRODDATE 0x08
#define MYKEY_BLOCK_LOCKID 0x05
#define MYKEY_BLOCK_VENDOR1 0x18
#define MYKEY_BLOCK_VENDOR2 0x19
#define MYKEY_BLOCK_CREDIT1 0x21
#define MYKEY_BLOCK_CREDIT2 0x25
#define MYKEY_BLOCK_TRANS_PTR 0x3C

// Reset values for detection
#define MYKEY_BLOCK18_RESET 0x8FCD0F48
#define MYKEY_BLOCK19_RESET 0xC0820007
```

## Technical Notes

### UID Format
SRIX4K UID is 8 bytes. Manufacturer bytes must be 0xD0 0x02 for SRIX4K compliance.

### Encryption
All encrypted blocks use XOR with the calculated encryption key. The encodeDecodeBlock function is its own inverse (applying it twice returns the original value).

### Checksums
First byte of most blocks contains a checksum calculated as:
```
0xFF - blockNum - (sum of all nibbles in remaining 3 bytes)
```

### Transaction History
Blocks 0x34-0x3B store up to 8 transactions in a circular buffer. Block 0x3C stores the pointer (0-7) to the next transaction slot.

### Credit Format
- Stored as cents in 16-bit value
- Upper 16 bits of block: credit amount
- Lower 16 bits: date stamp (days since 1/1/1995)
- Display as €XX.XX format

## Usage Workflow

### Reading a Tag
1. Navigate to RFID menu
2. Select "MAVAI Tool"
3. Select "Read Tag"
4. Place tag on reader
5. Wait for read completion
6. View quick info (credit, vendor)

### Managing Credit
1. Read tag first (or load dump)
2. Select "Add Credit" or "Set Credit"
3. Enter amount in cents
4. Confirm operation
5. Write back to tag using "Clone Tag"

### Managing Vendor
1. Read tag or load dump
2. Select "View Vendor" to see current
3. Select "Import Vendor" to change
4. Enter vendor code in hex
5. Write back to tag

### Factory Reset
1. Read tag or load dump
2. Select "Reset Key"
3. Review warning
4. Confirm operation
5. Write back to tag

## Security Considerations

### 1. Lock ID Protection
Always check lock status (block 0x05) before attempting modifications. A locked card (0x7F) may reject write operations.

### 2. Backup Before Changes
Always save a dump file before making any modifications to ensure data recovery is possible.

### 3. Vendor Code Integrity
Importing an incorrect vendor code may break card functionality with dependent systems. Verify vendor codes before import.

### 4. Write Verification
After writing changes, always read the card back to verify the write operation succeeded.

### 5. Date/Time Accuracy
Transaction timestamps use the system clock. Ensure device time is accurate before adding credits.

### 6. Transaction History
The system logs all credit additions to the transaction history. This provides an audit trail but can fill up (8 slots).

### 7. Encryption Key Exposure
The calculated encryption key is displayed in MyKey Info. This is sensitive information that should be protected.

## Hardware Requirements

- **PN532 Module**: I2C connection required
- **SRIX4K Tags**: Compatible with ST25TB04K/SRIX4K chips
- **Connection**: PN532 module must be configured for I2C mode in device settings

## Integration

### Menu Integration
MAVAI is automatically added to the RFID menu when:
- RFID module is set to PN532_I2C_MODULE
- Firmware is not in LITE_VERSION mode

### Files Created
- `src/modules/rfid/mavai.h`: Header file with class and constants
- `src/modules/rfid/mavai.cpp`: Implementation (1600+ lines)
- `src/core/menu_items/RFIDMenu.cpp`: Menu integration
- `src/core/wifi/webInterface.cpp`: API endpoints

## References

1. **SRIX Tool**: Based on BruceDevices/firmware PR #1983
2. **MIKAI Library**: mikai.c, mykey.c, srix.c - MyKey algorithms and data parsing
3. **ST25TB04K Datasheet**: SRIX4K memory map and specifications
4. **MyKey System**: Vendor-based encryption and credit management system

## Known Limitations

1. **Hardware Access**: WebGUI endpoints are placeholders - actual tag operations require physical hardware
2. **Date Format**: Production date parsing assumes specific BCD format
3. **Transaction History**: Limited to 8 transactions (circular buffer)
4. **Checksum Validation**: Implemented but not enforced during read operations

## Future Enhancements

- Full WebGUI implementation with real-time tag operations
- Enhanced transaction history viewer
- Vendor database management
- Multi-tag batch operations
- Enhanced error recovery and validation
- Transaction splitting for denominations

## Support

For issues, questions, or contributions related to MAVAI:
- Check existing SRIX Tool documentation
- Review MYKEY_IMPLEMENTATION.md for MyKey details
- Refer to PR #1983 for SRIX Tool background

## License

Same license as the main firmware project.
