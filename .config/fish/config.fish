if test -d $HOME/.local/bin
    fish_add_path $HOME/.local/bin
end

set -gx EDITOR nvim
set -gx VISUAL nvim
set -gx SHELL /usr/bin/fish
set -gx MANPAGER "sh -c 'col -bx | bat -l man -p'"

if status is-interactive
    set -g fish_greeting

    # One clean Ryoku-style dossier per terminal, never in nested Fish shells.
    if not set -q __MANGO_FASTFETCH_SHOWN
        set -gx __MANGO_FASTFETCH_SHOWN 1
        fastfetch
    end

    starship init fish | source

    # Ctrl-R history, Ctrl-T files, and Alt-C directories.
    set -gx FZF_DEFAULT_COMMAND 'find . -type f -not -path "*/.git/*"'
    set -gx FZF_CTRL_T_COMMAND $FZF_DEFAULT_COMMAND
    set -gx FZF_ALT_C_COMMAND 'find . -type d -not -path "*/.git/*"'
    fzf --fish | source

    alias ls 'eza --icons=auto --group-directories-first'
    alias ll 'eza -lah --icons=auto --group-directories-first --git'
    alias la 'eza -a --icons=auto --group-directories-first'
    alias lt 'eza --tree --level=2 --icons=auto --group-directories-first'
    alias cat 'bat --paging=never'
    alias grep 'grep --color=auto'
    # Keep OMP in $HOME instead of its default temporary workspace.
    alias omp 'command omp --allow-home'

    abbr -a g git
    abbr -a ga 'git add'
    abbr -a gc 'git commit'
    abbr -a gp 'git push'
    abbr -a gst 'git status --short --branch'
    abbr -a c clear

    function mkcd --description 'Create a directory and enter it'
        mkdir -p -- $argv[1]; and cd -- $argv[1]
    end
end
