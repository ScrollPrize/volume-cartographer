#!/usr/bin/env python3
"""
AWS S3 Interactive Sync Tool with Conflict Resolution

Automatically ignores:
- Hidden files and directories (starting with .)
- Any directory containing 'layers' in its name (e.g., layers/, layers_fullres/, old_layers/)
- The .s3sync.json configuration file

Usage:
    python s3_sync.py init <directory> <s3_bucket> <s3_prefix> [--profile=<aws_profile>]
    python s3_sync.py status <directory> [--verbose]
    python s3_sync.py sync <directory> [--dry-run]
    python s3_sync.py update <directory>
"""

import os
import sys
import json
import hashlib
import argparse
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Set, Tuple, Optional, List
from enum import Enum


class SyncAction(Enum):
    UPLOAD = "upload"
    DOWNLOAD = "download"
    CONFLICT = "conflict"
    SKIP = "skip"
    DELETE_LOCAL = "delete_local"
    DELETE_REMOTE = "delete_remote"


class S3SyncManager:
    def __init__(self, local_dir: str, s3_bucket: str = None, s3_prefix: str = None,
                 aws_profile: Optional[str] = None, config_file: Optional[str] = None):
        self.local_dir = os.path.abspath(local_dir)

        # Config file defaults to .s3sync.json in the local directory
        if config_file is None:
            config_file = os.path.join(self.local_dir, '.s3sync.json')
        self.config_file = config_file

        # Load or create config
        if os.path.exists(self.config_file):
            self._load_config()
        else:
            if not s3_bucket or not s3_prefix:
                raise ValueError("s3_bucket and s3_prefix required for initialization")
            self.s3_bucket = s3_bucket
            self.s3_prefix = s3_prefix.rstrip('/')
            self.aws_profile = aws_profile
            self.files = {}
            self._save_config()

    def _load_config(self):
        """Load configuration from JSON file"""
        with open(self.config_file, 'r') as f:
            data = json.load(f)

        self.s3_bucket = data['s3_bucket']
        self.s3_prefix = data['s3_prefix']
        self.aws_profile = data.get('aws_profile')
        self.files = data.get('files', {})

    def _save_config(self):
        """Save configuration to JSON file"""
        data = {
            'local_dir': self.local_dir,
            's3_bucket': self.s3_bucket,
            's3_prefix': self.s3_prefix,
            'aws_profile': self.aws_profile,
            'files': self.files,
            'last_updated': datetime.now().isoformat()
        }

        with open(self.config_file, 'w') as f:
            json.dump(data, f, indent=2)

    def _run_aws_command(self, cmd: List[str]) -> subprocess.CompletedProcess:
        """Run AWS CLI command with optional profile"""
        if self.aws_profile:
            cmd.extend(['--profile', self.aws_profile])
        return subprocess.run(cmd, capture_output=True, text=True)

    def _get_s3_url(self, relative_path: Optional[str] = None) -> str:
        """Get S3 URL for a file or directory"""
        if relative_path:
            return f"s3://{self.s3_bucket}/{self.s3_prefix}/{relative_path}"
        return f"s3://{self.s3_bucket}/{self.s3_prefix}/"

    def _parse_timestamp(self, timestamp_str: str) -> float:
        """Parse AWS timestamp to Unix timestamp"""
        dt = datetime.fromisoformat(timestamp_str.replace('Z', '+00:00'))
        return dt.timestamp()

    def scan_local_files(self) -> Dict[str, Dict]:
        """Scan local directory for files"""
        print(f"Scanning local directory: {self.local_dir}")
        files = {}

        for root, dirs, filenames in os.walk(self.local_dir):
            # Skip hidden directories (starting with .) and directories containing 'layers' in the name
            dirs[:] = [d for d in dirs if not d.startswith('.') and 'layers' not in d.lower()]

            for filename in filenames:
                # Skip hidden files and the sync config file
                if filename.startswith('.') or filename == '.s3sync.json':
                    continue

                filepath = os.path.join(root, filename)
                relative_path = os.path.relpath(filepath, self.local_dir)

                # Double-check: skip files in directories containing 'layers' (in case of symlinks or other edge cases)
                path_parts = relative_path.split(os.sep)
                if any('layers' in part.lower() for part in path_parts[:-1]):
                    continue

                try:
                    stat = os.stat(filepath)
                    files[relative_path] = {
                        'path': relative_path,
                        'local_size': stat.st_size,
                        'local_mtime': stat.st_mtime
                    }
                except (OSError, IOError) as e:
                    print(f"  Warning: Could not process {filepath}: {e}")

        print(f"Found {len(files)} local files")
        return files

    def scan_s3_files(self) -> Dict[str, Dict]:
        """Scan S3 bucket for files with pagination support"""
        print(f"Scanning S3: s3://{self.s3_bucket}/{self.s3_prefix}/")
        files = {}
        continuation_token = None
        page_count = 0

        while True:
            cmd = [
                'aws', 's3api', 'list-objects-v2',
                '--bucket', self.s3_bucket,
                '--prefix', self.s3_prefix
            ]

            if continuation_token:
                cmd.extend(['--continuation-token', continuation_token])

            result = self._run_aws_command(cmd)

            if result.returncode != 0:
                print(f"Error listing S3 objects: {result.stderr}")
                break

            if not result.stdout:
                print("No files found in S3")
                break

            try:
                data = json.loads(result.stdout)
            except json.JSONDecodeError:
                print(f"Error parsing S3 response: {result.stdout}")
                break

            if 'Contents' not in data:
                if page_count == 0:
                    print("No files found in S3")
                break

            prefix_len = len(self.s3_prefix) + 1 if self.s3_prefix else 0

            for obj in data['Contents']:
                # Skip if it's just the prefix itself
                if obj['Key'] == self.s3_prefix + '/':
                    continue

                relative_path = obj['Key'][prefix_len:]

                # Skip hidden files (starting with .)
                filename = os.path.basename(relative_path)
                if filename.startswith('.'):
                    continue

                # Skip files in hidden directories or directories containing 'layers'
                path_parts = relative_path.split('/')
                if any(part.startswith('.') for part in path_parts[:-1]):
                    continue

                # Skip files in directories containing 'layers' in the name
                if any('layers' in part.lower() for part in path_parts[:-1]):
                    continue

                files[relative_path] = {
                    'path': relative_path,
                    's3_size': obj['Size'],
                    's3_mtime': self._parse_timestamp(obj['LastModified']),
                    's3_etag': obj.get('ETag', '').strip('"')
                }

            page_count += 1

            if not data.get('IsTruncated'):
                break

            continuation_token = data.get('NextContinuationToken')
            if not continuation_token:
                break

            if page_count % 10 == 0:
                print(f"  Scanned {len(files)} files so far...")

        print(f"Found {len(files)} S3 files")
        return files

    def update_files(self):
        """Update file tracking with current state"""
        print("\nUpdating file tracking...")

        local_files = self.scan_local_files()
        s3_files = self.scan_s3_files()

        # Get all paths
        current_paths = set(local_files.keys()) | set(s3_files.keys())
        tracked_paths = set(self.files.keys())

        # Update tracked files
        for path in current_paths:
            if path not in self.files:
                self.files[path] = {}

            file_info = self.files[path]

            if path in local_files:
                file_info['local_size'] = local_files[path]['local_size']
                file_info['local_mtime'] = local_files[path]['local_mtime']
            else:
                # Local file deleted
                file_info['local_size'] = None
                file_info['local_mtime'] = None

            if path in s3_files:
                file_info['s3_size'] = s3_files[path]['s3_size']
                file_info['s3_mtime'] = s3_files[path]['s3_mtime']
                file_info['s3_etag'] = s3_files[path]['s3_etag']
            else:
                # S3 file deleted
                file_info['s3_size'] = None
                file_info['s3_mtime'] = None
                file_info['s3_etag'] = None

        # Track files that were deleted from both
        for path in tracked_paths - current_paths:
            db_file = self.files[path]
            if db_file.get('local_size') is None and db_file.get('s3_size') is None:
                # Deleted from both, remove from tracking
                del self.files[path]

        self._save_config()
        print("File tracking updated successfully")

    def analyze_changes(self, local_files: Dict, s3_files: Dict) -> Dict[str, Tuple[SyncAction, str]]:
        """Analyze what needs to be synced and detect conflicts"""
        actions = {}

        # Get all paths
        all_paths = set(self.files.keys()) | set(local_files.keys()) | set(s3_files.keys())

        for path in all_paths:
            local_info = local_files.get(path)
            s3_info = s3_files.get(path)
            tracked_info = self.files.get(path, {})

            # File only exists locally
            if local_info and not s3_info:
                if tracked_info.get('s3_size') is not None:
                    # Was on S3, now deleted
                    actions[path] = (SyncAction.DELETE_LOCAL, "S3 file was deleted")
                else:
                    actions[path] = (SyncAction.UPLOAD, "New local file")

            # File only exists on S3
            elif s3_info and not local_info:
                if tracked_info.get('local_size') is not None:
                    # Was local, now deleted
                    actions[path] = (SyncAction.DELETE_REMOTE, "Local file was deleted")
                else:
                    actions[path] = (SyncAction.DOWNLOAD, "New S3 file")

            # File exists in both places
            elif local_info and s3_info:
                if tracked_info:
                    # We have tracking history - check for changes
                    local_changed = (tracked_info.get('local_size') != local_info['local_size'] or
                                     (tracked_info.get('local_mtime') and
                                      abs(tracked_info['local_mtime'] - local_info['local_mtime']) > 1))

                    # For S3, check both size and etag (etag is more reliable than mtime)
                    s3_changed = (tracked_info.get('s3_size') != s3_info['s3_size'] or
                                  tracked_info.get('s3_etag') != s3_info['s3_etag'])

                    if local_changed and s3_changed:
                        # Both changed since last sync - true conflict
                        actions[path] = (SyncAction.CONFLICT, "Both local and S3 modified since last sync")
                    elif local_changed:
                        actions[path] = (SyncAction.UPLOAD, "Local file modified")
                    elif s3_changed:
                        actions[path] = (SyncAction.DOWNLOAD, "S3 file modified")
                    else:
                        # Neither has changed
                        actions[path] = (SyncAction.SKIP, "Files are in sync")
                else:
                    # No tracking history - check if files are different
                    if local_info['local_size'] != s3_info['s3_size']:
                        # Different sizes - not a conflict, just different
                        # We don't know who changed, so ask user
                        actions[path] = (SyncAction.CONFLICT, "Files differ (no sync history)")
                    else:
                        # Same size, assume in sync
                        actions[path] = (SyncAction.SKIP, "Files appear to be in sync")

            # File deleted from both
            elif path in self.files and not local_info and not s3_info:
                # Remove from tracking
                actions[path] = (SyncAction.SKIP, "File deleted from both")

        return actions

    def resolve_conflict(self, path: str, reason: str, local_info: Optional[Dict],
                         s3_info: Optional[Dict]) -> SyncAction:
        """Interactively resolve a conflict"""
        print(f"\n⚠️  CONFLICT: {path}")
        print(f"Reason: {reason}")

        if local_info and s3_info:
            # Both exist with differences
            print(f"  Local:  Size={local_info['local_size']:,} bytes, "
                  f"Modified={datetime.fromtimestamp(local_info['local_mtime'])}")
            print(f"  S3:     Size={s3_info['s3_size']:,} bytes, "
                  f"Modified={datetime.fromtimestamp(s3_info['s3_mtime'])}")

            # For true conflicts (both changed), provide context
            if "both" in reason.lower():
                print("  ⚠️  Both files have been modified since last sync!")

            while True:
                response = input("\nChoose: [l]ocal → S3, [r]emote → local, [s]kip? ").strip().lower()
                if response == 'l':
                    return SyncAction.UPLOAD
                elif response == 'r':
                    return SyncAction.DOWNLOAD
                elif response == 's':
                    return SyncAction.SKIP
                else:
                    print("Invalid choice. Please enter 'l', 'r', or 's'.")

        return SyncAction.SKIP

    def perform_upload(self, path: str) -> bool:
        """Upload a single file to S3"""
        local_path = os.path.join(self.local_dir, path)
        s3_path = self._get_s3_url(path)

        print(f"  Uploading: {path} → S3")

        cmd = ['aws', 's3', 'cp', local_path, s3_path]
        result = self._run_aws_command(cmd)

        if result.returncode == 0:
            print(f"  ✓ Uploaded: {path}")
            return True
        else:
            print(f"  ✗ Error uploading {path}: {result.stderr}")
            return False

    def perform_download(self, path: str) -> bool:
        """Download a single file from S3"""
        local_path = os.path.join(self.local_dir, path)
        s3_path = self._get_s3_url(path)

        # Create directory if needed
        os.makedirs(os.path.dirname(local_path), exist_ok=True)

        print(f"  Downloading: S3 → {path}")

        cmd = ['aws', 's3', 'cp', s3_path, local_path]
        result = self._run_aws_command(cmd)

        if result.returncode == 0:
            print(f"  ✓ Downloaded: {path}")
            return True
        else:
            print(f"  ✗ Error downloading {path}: {result.stderr}")
            return False

    def perform_delete_local(self, path: str) -> bool:
        """Delete a local file"""
        local_path = os.path.join(self.local_dir, path)

        print(f"  Deleting local: {path}")

        try:
            os.remove(local_path)
            print(f"  ✓ Deleted local: {path}")
            return True
        except Exception as e:
            print(f"  ✗ Error deleting {path}: {e}")
            return False

    def perform_delete_remote(self, path: str) -> bool:
        """Delete a file from S3"""
        s3_path = self._get_s3_url(path)

        print(f"  Deleting from S3: {path}")

        cmd = ['aws', 's3', 'rm', s3_path]
        result = self._run_aws_command(cmd)

        if result.returncode == 0:
            print(f"  ✓ Deleted from S3: {path}")
            return True
        else:
            print(f"  ✗ Error deleting {path}: {result.stderr}")
            return False

    def sync(self, dry_run: bool = False):
        """Perform interactive sync operation"""
        print("\nAnalyzing changes...")

        local_files = self.scan_local_files()
        s3_files = self.scan_s3_files()

        actions = self.analyze_changes(local_files, s3_files)

        # Separate actions by type
        uploads = []
        downloads = []
        deletes_local = []
        deletes_remote = []
        conflicts = []

        for path, (action, reason) in sorted(actions.items()):
            if action == SyncAction.UPLOAD:
                uploads.append((path, reason))
            elif action == SyncAction.DOWNLOAD:
                downloads.append((path, reason))
            elif action == SyncAction.DELETE_LOCAL:
                deletes_local.append((path, reason))
            elif action == SyncAction.DELETE_REMOTE:
                deletes_remote.append((path, reason))
            elif action == SyncAction.CONFLICT:
                conflicts.append((path, reason))

        # Summary
        print(f"\nSync Summary:")
        print(f"  Uploads pending:    {len(uploads)}")
        print(f"  Downloads pending:  {len(downloads)}")
        print(f"  Local deletions:    {len(deletes_local)}")
        print(f"  Remote deletions:   {len(deletes_remote)}")
        print(f"  Conflicts:          {len(conflicts)}")

        if not any([uploads, downloads, deletes_local, deletes_remote, conflicts]):
            print("\n✓ Everything is in sync!")
            return

        if dry_run:
            print("\n--dry-run mode: No changes will be made")
            return

        # Process conflicts first
        resolved_actions = []
        for path, reason in conflicts:
            local_info = local_files.get(path)
            s3_info = s3_files.get(path)

            action = self.resolve_conflict(path, reason, local_info, s3_info)
            if action != SyncAction.SKIP:
                resolved_actions.append((path, action))

        # Confirm before proceeding
        total_operations = (len(uploads) + len(downloads) + len(deletes_local) +
                            len(deletes_remote) + len(resolved_actions))

        print(f"\n{total_operations} operations will be performed.")
        response = input("Continue? [y/N]: ").strip().lower()

        if response != 'y':
            print("Sync cancelled.")
            return

        # Perform operations and update tracking for each successful operation
        print("\nSyncing...")
        success_count = 0

        # Process uploads
        for path, reason in uploads:
            if self.perform_upload(path):
                success_count += 1
                # Update tracking for this file
                if path not in self.files:
                    self.files[path] = {}
                # Get fresh S3 info after upload
                cmd = ['aws', 's3api', 'head-object', '--bucket', self.s3_bucket,
                       '--key', f"{self.s3_prefix}/{path}"]
                result = self._run_aws_command(cmd)
                if result.returncode == 0:
                    try:
                        data = json.loads(result.stdout)
                        s3_mtime = self._parse_timestamp(data['LastModified'])
                        s3_etag = data.get('ETag', '').strip('"')
                    except:
                        s3_mtime = datetime.now().timestamp()
                        s3_etag = None
                else:
                    s3_mtime = datetime.now().timestamp()
                    s3_etag = None

                self.files[path].update({
                    'local_size': local_files[path]['local_size'],
                    'local_mtime': local_files[path]['local_mtime'],
                    's3_size': local_files[path]['local_size'],
                    's3_mtime': s3_mtime,
                    's3_etag': s3_etag
                })

        # Process downloads
        for path, reason in downloads:
            if self.perform_download(path):
                success_count += 1
                # Update tracking for this file
                if path not in self.files:
                    self.files[path] = {}
                self.files[path].update({
                    'local_size': s3_files[path]['s3_size'],
                    'local_mtime': datetime.now().timestamp(),  # Will be current time
                    's3_size': s3_files[path]['s3_size'],
                    's3_mtime': s3_files[path]['s3_mtime'],
                    's3_etag': s3_files[path].get('s3_etag')
                })

        # Process deletions
        for path, reason in deletes_local:
            if self.perform_delete_local(path):
                success_count += 1
                # Remove from tracking
                if path in self.files:
                    del self.files[path]

        for path, reason in deletes_remote:
            if self.perform_delete_remote(path):
                success_count += 1
                # Remove from tracking
                if path in self.files:
                    del self.files[path]

        # Process resolved conflicts
        for path, action in resolved_actions:
            success = False
            if action == SyncAction.UPLOAD:
                success = self.perform_upload(path)
                if success and path in local_files:
                    if path not in self.files:
                        self.files[path] = {}
                    # Get fresh S3 info after upload
                    cmd = ['aws', 's3api', 'head-object', '--bucket', self.s3_bucket,
                           '--key', f"{self.s3_prefix}/{path}"]
                    result = self._run_aws_command(cmd)
                    if result.returncode == 0:
                        try:
                            data = json.loads(result.stdout)
                            s3_mtime = self._parse_timestamp(data['LastModified'])
                            s3_etag = data.get('ETag', '').strip('"')
                        except:
                            s3_mtime = datetime.now().timestamp()
                            s3_etag = None
                    else:
                        s3_mtime = datetime.now().timestamp()
                        s3_etag = None

                    self.files[path].update({
                        'local_size': local_files[path]['local_size'],
                        'local_mtime': local_files[path]['local_mtime'],
                        's3_size': local_files[path]['local_size'],
                        's3_mtime': s3_mtime,
                        's3_etag': s3_etag
                    })
            elif action == SyncAction.DOWNLOAD:
                success = self.perform_download(path)
                if success and path in s3_files:
                    if path not in self.files:
                        self.files[path] = {}
                    self.files[path].update({
                        'local_size': s3_files[path]['s3_size'],
                        'local_mtime': datetime.now().timestamp(),
                        's3_size': s3_files[path]['s3_size'],
                        's3_mtime': s3_files[path]['s3_mtime'],
                        's3_etag': s3_files[path].get('s3_etag')
                    })
            elif action == SyncAction.DELETE_LOCAL:
                success = self.perform_delete_local(path)
                if success and path in self.files:
                    del self.files[path]
            elif action == SyncAction.DELETE_REMOTE:
                success = self.perform_delete_remote(path)
                if success and path in self.files:
                    del self.files[path]

            if success:
                success_count += 1

        # Save updated tracking
        self._save_config()

        print(f"\n✓ Sync complete: {success_count}/{total_operations} operations successful")

    def show_status(self, verbose: bool = False):
        """Show sync status"""
        print(f"S3 Sync Status")
        print(f"Local directory: {self.local_dir}")
        print(f"S3 location: s3://{self.s3_bucket}/{self.s3_prefix}/")

        if self.aws_profile:
            print(f"AWS Profile: {self.aws_profile}")

        print("\nAnalyzing changes...")

        local_files = self.scan_local_files()
        s3_files = self.scan_s3_files()
        actions = self.analyze_changes(local_files, s3_files)

        # Count actions
        action_counts = {}
        for path, (action, reason) in actions.items():
            action_counts[action] = action_counts.get(action, 0) + 1

        print(f"\nSummary:")
        print(f"  Files to upload:     {action_counts.get(SyncAction.UPLOAD, 0)}")
        print(f"  Files to download:   {action_counts.get(SyncAction.DOWNLOAD, 0)}")
        print(f"  Files to delete (S3): {action_counts.get(SyncAction.DELETE_REMOTE, 0)}")
        print(f"  Files to delete (local): {action_counts.get(SyncAction.DELETE_LOCAL, 0)}")
        print(f"  Conflicts:           {action_counts.get(SyncAction.CONFLICT, 0)}")
        print(f"  In sync:             {action_counts.get(SyncAction.SKIP, 0)}")

        if verbose:
            # Show detailed file list
            for action in [SyncAction.UPLOAD, SyncAction.DOWNLOAD, SyncAction.DELETE_REMOTE,
                           SyncAction.DELETE_LOCAL, SyncAction.CONFLICT]:
                files = [(p, r) for p, (a, r) in actions.items() if a == action]
                if files:
                    print(f"\n{action.value.replace('_', ' ').title()} ({len(files)} files):")
                    for path, reason in sorted(files):
                        print(f"  {path}: {reason}")


def main():
    parser = argparse.ArgumentParser(description='AWS S3 interactive sync with conflict resolution')
    subparsers = parser.add_subparsers(dest='command', help='Commands')

    # Init command
    init_parser = subparsers.add_parser('init', help='Initialize sync configuration')
    init_parser.add_argument('directory', help='Local directory to sync')
    init_parser.add_argument('s3_bucket', help='S3 bucket name')
    init_parser.add_argument('s3_prefix', help='S3 prefix (path within bucket)')
    init_parser.add_argument('--profile', help='AWS profile to use')

    # Status command
    status_parser = subparsers.add_parser('status', help='Show sync status')
    status_parser.add_argument('directory', help='Local directory')
    status_parser.add_argument('--verbose', '-v', action='store_true', help='Show detailed file list')

    # Sync command
    sync_parser = subparsers.add_parser('sync', help='Perform interactive sync')
    sync_parser.add_argument('directory', help='Local directory')
    sync_parser.add_argument('--dry-run', action='store_true', help='Show what would be synced without doing it')

    # Update command
    update_parser = subparsers.add_parser('update', help='Update file tracking with current state')
    update_parser.add_argument('directory', help='Local directory')

    # Reset command
    reset_parser = subparsers.add_parser('reset', help='Reset sync tracking (mark all as synced)')
    reset_parser.add_argument('directory', help='Local directory')

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    if args.command == 'init':
        # Initialize new sync configuration
        manager = S3SyncManager(args.directory, args.s3_bucket, args.s3_prefix, args.profile)
        print(f"Initialized sync configuration in {args.directory}")
        print(f"S3 location: s3://{args.s3_bucket}/{args.s3_prefix}/")

        # Do initial tracking update
        manager.update_files()

        print("\nUse 'status' command to see current sync state")

    else:
        # Check for existing configuration
        config_file = os.path.join(args.directory, '.s3sync.json')

        if not os.path.exists(config_file):
            print(f"Error: No sync configuration found in {args.directory}")
            print("Run 'init' command first to set up sync configuration")
            sys.exit(1)

        manager = S3SyncManager(args.directory)

        if args.command == 'status':
            manager.show_status(args.verbose)

        elif args.command == 'sync':
            manager.sync(args.dry_run)

        elif args.command == 'update':
            manager.update_files()

        elif args.command == 'reset':
            print("Resetting sync tracking...")
            print("This will mark all current files as synced.")
            response = input("Continue? [y/N]: ").strip().lower()

            if response == 'y':
                manager.update_files()
                print("✓ Sync tracking reset. All files marked as in sync.")
            else:
                print("Reset cancelled.")


if __name__ == "__main__":
    main()