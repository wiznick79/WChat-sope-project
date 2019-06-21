Operating Systems Project Phase 2 by Nikolaos Perris (#36261) and Alvaro Magalhaes (#37000)


Known issues: 
- changename not properly working, as it removes the user from all channels
- GUI related random crashes that we still try to figure out the problem
- memory related random crashes that we are slowly fixing


v0.88 - 19/06/2019
- Various bug fixes
- Removed owner pointer from chatroom structure, cause members[0] is always the owner anyway
- Disconnect menu option is now working
- /showrooms and /showusers commands print messages in main window instead of updating combobox and userlist
- Ban user function improved, also /banlist command added on server to check banned users for chatrooms
- Added some semaphores stuff for the Producer/Consumer requirement (not fully working)

v0.87 - 17/06/2019
- Users can create and join multiple rooms
- Ban user from room fixed
- Some bug fixes


v0.86 - 12/06/2019
- Version uploaded on e-learning platform
- Users are able to join or create only one room
- Some functions not fully working
