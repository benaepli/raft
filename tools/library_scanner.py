#!/usr/bin/env python3
import os
import re
from pathlib import Path
from typing import List, Set


def find_source_files(directories: List[str], extensions: Set[str] = {'.cpp', '.hpp', '.h', '.cc', '.cxx'}) -> List[
    str]:
    """
    Recursively find all source files in the given directories.

    Args:
        directories: List of directory paths to scan
        extensions: Set of file extensions to include

    Returns:
        Sorted list of relative file paths
    """
    files = []

    for directory in directories:
        path = Path(directory)
        if not path.exists():
            print(f"Warning: Directory {directory} does not exist")
            continue

        # Walk through directory recursively
        for file_path in path.rglob('*'):
            if file_path.is_file() and file_path.suffix.lower() in extensions:
                # Get relative path from current directory
                try:
                    relative_path = file_path.relative_to(Path.cwd())
                except ValueError:
                    # If not relative to cwd, use the path as is
                    relative_path = file_path

                # Convert to forward slashes for CMake compatibility
                files.append(str(relative_path).replace('\\', '/'))

    return sorted(files)


def format_add_library(library_name: str, files: List[str], indent: str = "    ") -> str:
    """
    Format the add_library declaration with proper indentation.

    Args:
        library_name: Name of the library
        files: List of source file paths
        indent: Indentation string (default 4 spaces)

    Returns:
        Formatted add_library declaration
    """
    if not files:
        return f"add_library({library_name})"

    lines = [f"add_library({library_name}"]
    for file in files:
        lines.append(f"{indent}{file}")
    lines.append(")")

    return '\n'.join(lines)


def replace_library_declaration(library_name: str, directories: List[str],
                                cmake_file: str = None,
                                extensions: Set[str] = {'.cpp', '.hpp', '.h', '.cc', '.cxx'}) -> str:
    """
    Generate or replace an add_library declaration by scanning directories for source files.

    Args:
        library_name: Name of the library
        directories: List of directories to scan for source files
        cmake_file: Optional CMakeLists.txt file to update in-place
        extensions: Set of file extensions to include

    Returns:
        The formatted add_library declaration
    """
    # Find all source files
    files = find_source_files(directories, extensions)

    # Format the declaration
    declaration = format_add_library(library_name, files)

    # If cmake_file is provided, update it in place
    if cmake_file and os.path.exists(cmake_file):
        with open(cmake_file, 'r') as f:
            content = f.read()

        # Pattern to match add_library declaration for this library
        pattern = rf'add_library\s*\(\s*{re.escape(library_name)}[^)]*\)'

        # Find if declaration exists
        match = re.search(pattern, content, re.DOTALL)

        if match:
            # Replace existing declaration
            new_content = content[:match.start()] + declaration + content[match.end():]

            with open(cmake_file, 'w') as f:
                f.write(new_content)

            print(f"Updated {cmake_file} with new add_library declaration for {library_name}")
        else:
            print(f"No existing add_library declaration found for {library_name} in {cmake_file}")

    return declaration


if __name__ == "__main__":
    os.chdir("raft")
    declaration = replace_library_declaration("raft", ["../include", "src"], "CMakeLists.txt")
    print(declaration)
