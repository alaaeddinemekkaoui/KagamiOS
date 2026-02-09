# Kagami OS - Command Reference

**Total Commands:** 18

---

## System Information

### help
Display this command list with brief descriptions
- **Usage:** `help`
- **Supports:** `-h`, `--help`

### logo
Display OS logo and system information
- **Usage:** `logo`
- **Supports:** `-h`, `--help`

### status
Show system vitals and current user information
- **Usage:** `status`
- **Displays:** Memory, file count, current user, current path
- **Supports:** `-h`, `--help`

### whoami
Display current user identity and role
- **Usage:** `whoami`
- **Supports:** `-h`, `--help`

---

## File System Navigation

### pwd
Print working directory - shows current location
- **Usage:** `pwd`
- **Example:** `pwd`
- **Supports:** `-h`, `--help`

### ls
List files in current directory (5 per row, folders marked with /)
- **Usage:** `ls`
- **Notes:** 
  - Shows only files in current directory
  - Folders displayed with trailing `/`
- **Supports:** `-h`, `--help`

### tree
Display directory tree structure with indentation
- **Usage:** `tree`
- **Notes:** Shows entire file system hierarchy
- **Supports:** `-h`, `--help`

### cd
Change directory to specified folder
- **Usage:** `cd <folder>`
- **Examples:**
  ```bash
  cd documents     # Enter documents folder
  cd ..           # Go to parent directory
  cd .            # Stay in current directory
  ```
- **Supports:** `-h`, `--help`

---

## File Operations

### read
Read and display file contents
- **Usage:** `read <file>`
- **Example:** `read readme.txt`
- **Notes:** Cannot read folders
- **Supports:** `-h`, `--help`

### create
Create a new file or folder
- **Usage:**
  ```bash
  create file.txt           # Create a file
  create folder/            # Create a folder (trailing /)
  create dir/file.txt       # Create file in directory (with prompt)
  ```
- **Features:** Interactive prompts for missing folders
- **Supports:** `-h`, `--help`

### write
Write text to a file (overwrites existing content)
- **Usage:** `write <file> <text>`
- **Example:** `write test.txt Hello from Kagami OS`
- **Warning:** ⚠️ Overwrites all existing content!
- **Notes:** File must exist first (use `create` if needed)
- **Supports:** `-h`, `--help`

### copy
Copy a file to a new name in current directory
- **Usage:** `copy <source> <destination>`
- **Example:** `copy readme.txt backup.txt`
- **Notes:** Only copies files, not folders
- **Supports:** `-h`, `--help`

### find
Search for files by name pattern across entire system
- **Usage:** `find <pattern>`
- **Example:** `find readme`
- **Notes:** Shows full path for each match
- **Supports:** `-h`, `--help`

### rm
Delete a file
- **Usage:** `rm <file>`
- **Example:** `rm oldfile.txt`
- **Warning:** ⚠️ Cannot be undone!
- **Supports:** `-h`, `--help`

---

## Utility Commands

### echo
Display text message
- **Usage:** `echo <text>`
- **Example:** `echo Hello World`
- **Supports:** `-h`, `--help`

### clear
Clear screen and show minimal header
- **Usage:** `clear`
- **Notes:** Displays current path after clearing
- **Supports:** `-h`, `--help`

---

## User Management

### useradd
Create a new user with home directory
- **Usage:** `useradd <username>`
- **Example:** `useradd alice`
- **Features:**
  - Automatically creates `/home/<username>/` directory
  - Interactive password prompt
- **Supports:** `-h`, `--help`

### login
Switch to different user and their home directory
- **Usage:** `login <username>`
- **Example:** `login root`
- **Features:**
  - Changes to user's home directory automatically
  - Interactive password prompt
- **Supports:** `-h`, `--help`

---

## Help System

All commands support detailed help:
```bash
<command> -h        # Show command help
<command> --help    # Show command help
```

**Examples:**
```bash
ls -h
create --help
pwd -h
```

---

## File System Structure

### Default Structure
```
/
└── home/
    └── root/
        ├── readme.txt
        ├── welcome.txt
        ├── spellbook.txt
        └── documents/
            └── secret.txt
```

### User Home Directories
- New users get: `/home/<username>/`
- Root user home: `/home/root/`

### Maximum Limits
| Item | Limit |
|------|-------|
| Total files/folders | Limited by heap memory (default ~1MB) |
| Filename length | 32 characters |
| File content | 256 characters |
| Parent path | 64 characters |

---

## Special Features

### Prompt Display
Shows username and path with tilde abbreviation:
- `kagami:~>` — in `/home/root`
- `kagami:~/documents>` — in `/home/root/documents`
- `alice:~>` — in `/home/alice`

### Auto-Scrolling
- Screen automatically scrolls up when reaching bottom
- Preserves command history
- 100-pixel scroll increments

### Interactive Prompts
- `create` command prompts for folder creation
- User management commands prompt for passwords
- Y/N responses accepted

---

## Keyboard Shortcuts

| Key | Function |
|-----|----------|
| **Backspace** | Delete last character |
| **Enter** | Execute command |
| **Letters** | Standard input (A-Z, a-z) |
| **Numbers** | Standard input (0-9) |
| **Symbols** | Space, /, -, _, ., and more |

### Shift Support
- Capital letters
- Shifted symbols (!, @, #, $, %, etc.)

---

## Command Quick Reference

| Category | Commands |
|----------|----------|
| **System** | help, logo, status, whoami |
| **Navigation** | pwd, ls, tree, cd |
| **File Ops** | read, create, write, copy, find, rm |
| **Utility** | echo, clear |
| **Users** | useradd, login |

---

## Usage Examples

### Basic File Management
```bash
pwd                          # Check current location
ls                           # List files here
create myfile.txt            # Create a file
write myfile.txt Hello!      # Write to file
read myfile.txt              # Read file content
copy myfile.txt backup.txt   # Duplicate file
rm myfile.txt                # Delete file
```

### Directory Navigation
```bash
pwd                    # Current directory
ls                     # List contents
cd documents           # Enter folder
pwd                    # Now in documents
cd ..                  # Go back up
```

### Advanced File Operations
```bash
create projects/              # Create folder (with /)
create projects/app.txt       # Create file in folder (prompts for folder if missing)
find readme                   # Search for "readme" in all files
tree                          # View entire structure
```

### User Management
```bash
useradd alice          # Create new user (prompts for password)
login alice            # Switch to alice (prompts for password)
whoami                 # Verify current user
pwd                    # Now in /home/alice
```

---

## Version Info

- **OS Name:** Kagami OS
- **Type:** Custom x86_64 Operating System
- **Boot:** UEFI
- **Display:** 1280×800 framebuffer (32-bit ARGB)
- **Keyboard:** PS/2 polling
- **Shell:** Unified framebuffer shell

---

## Important Notes

⚠️ **Warnings:**
- Commands are case-sensitive (use lowercase)
- File system is in-memory (not persistent across reboots)
- `write` and `rm` commands cannot be undone
- Total files/folders limited by heap memory

💡 **Tips:**
- Use `<command> -h` for detailed help on any command
- Folders always end with `/` in listings
- Nested paths supported: `dir/file.txt`
- Parent directory: Use `..` with `cd` command
- Files are created in current directory by default

---

## Technical Details

### Architecture
- **Platform:** x86_64
- **Boot Protocol:** UEFI
- **Graphics:** Direct framebuffer rendering
- **Input:** PS/2 keyboard with scancode conversion

### Shell Features
- Unified command shell (single file: `shell.c`)
- Auto-scrolling display (100px increments)
- Dynamic prompt with home directory abbreviation
- Command history preserved during scrolling
- Interactive Y/N prompts for confirmations

### File System
- Virtual in-memory file system
- Parent directory tracking for proper navigation
- Support for nested directories
- Automatic home directory creation for new users

---

**Last Updated:** February 8, 2026  
**Documentation Version:** 1.0
