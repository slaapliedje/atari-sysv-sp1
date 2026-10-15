" mail.vim - after vim's own syntax/mail.vim on AMIX and Atari System V
" (atari-sysv-sp1 amix/vim), installed as
" /usr/local/share/vim/vimfiles/after/syntax/mail.vim.
"
" On the TT's 68030, colouring a screen of quoted mail took 3-4.5 s, past
" 'redrawtime', after which vim stops colouring the buffer, and a jump to
" the end of a long one 8 s. Most of it was:
"
" - the emoji, a pattern of Unicode ranges that always runs on the NFA
"   engine: 2.6 s of 4.4. A Latin-1 terminal has no emoji to show, so it
"   goes.
" - the addresses, which from each column inside a word read on to its end
"   looking for an "@": 1.1 s of 3.2. Tried only where a word starts, they
"   take about a quarter of that and find the same addresses (one starting
"   with "." "+" "=" "/" or "-" is coloured from its first letter on).
" - the 100 lines vim reads back from the top of the window, after a jump,
"   to learn where it is (sync). A quoted line starts its quote over, so 30
"   are plenty; links and addresses never span lines, so they are left out
"   of that reading ("display").
if &termencoding !=# 'utf-8'
  syn clear mailEmoji
endif
syn case ignore
syn clear mailEmail
syn match mailEmail display contains=@NoSpell "\v<[_=a-z\./+0-9-]+\@[a-z0-9._-]+\a{2}"
syn clear mailURL
syn match mailURL display contains=@NoSpell `\v<(((https?|ftp|gopher)://|(mailto|file|news):)[^' 	<>"]+|(www|web|w3)[a-z0-9_-]*\.[a-z0-9._-]+\.[^' 	<>"]+)[a-z0-9/]`
if !exists("mail_minlines")
  syn sync minlines=30
endif
