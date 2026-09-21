#
# ~/.bashrc
#

# If not running interactively, don't do anything
[[ $- != *i* ]] && return

# alias ls='ls --color=auto'
# alias grep='grep --color=auto'
# PS1='[\u@\h \W]\$ '
export PATH="$HOME/.local/bin:$PATH"

# Keep OMP in the directory from which the shell launched it. OMP otherwise
# redirects sessions started in $HOME to a temporary directory.
omp() {
    command "$HOME/.local/bin/omp" --allow-home "$@"
}
