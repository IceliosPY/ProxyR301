# Proxy
Un proxy entre plusieurs clients et un serveur ftp afin de récupérer les fichiers de ce dernier.

1. Lancer le programme en faisant make puis ./proxy.
2. Vous avez lancé le proxy et une adresse IP avec un PORT s'affiche, pour tester vous pouvez ouvrir plusiseurs cmd
et taper ftp -d [ip] [port].
4. Après cela le serveur demande un LOGIN qui est donc votre nom, pour tester vous pouvez utiliser anonymous.
5. Collez à cela il faut ajoute @[domaine] pour nos testes nous avons utilisé @ftp.fau.de donc nous envoyes anonymous@ftp.fau.de
6. Le serveur demande ensuite un mdp et vous pouvez taper ce que vous voulez.
7. Ensuite notre but a été de récupérer les fichiers du serveur donc dans la console cliente nous executons la commande ls
8. Le proxy fait ensuite le travail et transfert les fichiers du serveur.

Le capacité de gérer plusieurs clients est fait en utilisant des processus fils avec la fonction fork()
