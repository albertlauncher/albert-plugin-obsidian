# Albert plugin: Obsidian

## Features

- Automatically finds vaults on load.
- Indexes vaults and notes by name and path.
- Watches vaults for changes.
- Vault actions:
    - Open vault.
    - Open vault and start search.
- Note actions:
    - Open note.
- Create new notes with filename or path with triggered query.

## Installation Support

This plugin supports both standard Obsidian installations and Flatpak installations:

- **Standard installation**: Looks for `~/.config/obsidian/obsidian.json`
- **Flatpak installation**: Looks for `~/.var/app/md.obsidian.Obsidian/config/obsidian/obsidian.json`

The plugin automatically detects which installation type you have and uses the appropriate configuration path.

## Technical notes  

Uses [Obsidian URIs](https://help.obsidian.md/Extending+Obsidian/Obsidian+URI) to interface Obsidian.
