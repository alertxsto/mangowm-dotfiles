function __mango_reload_theme --on-event fish_prompt
    set -l palette "$HOME/.config/fish/conf.d/theme-colors.fish"
    test -r "$palette"; or return
    set -l signature (command sha256sum "$palette" | string split " ")[1]
    test "$signature" = "$__mango_theme_signature"; and return
    source "$palette"
    set -g __mango_theme_signature "$signature"
end

__mango_reload_theme
