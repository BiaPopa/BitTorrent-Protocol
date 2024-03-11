Protocolul BitTorrent

### 1. Detalii implementare clienti

### Initializare

1. Citirea fisierului de intrare are loc in functia peer, unde
variabilele have_files si want_files retin numarul de fisiere pe care
clientul le detine, respectiv numarul de fisiere pe care isi doreste
sa le descarce. Informatiile despre aceste fisiere vor fi retinute
in variabilele files si download_files.
2. Ii spune tracker-ului ce fisiere are. Initial, clientul ii
transmite tracker-ului numarul de fisiere pe care le detine. Apoi,
trimite, in ordine, datele fiecarui fisier detinut: nume, numarul de
segmente si segmentele in sine (pozitia si hash-ul).
3. Asteapta raspuns de la tracker. In momentul in care toti clientii 
si-au transmis datele, acestia primesc un mesaj "ACK" de la tracker.

### Descarcare

Aceasta este implementata in functia download_thread_func. Se parcurg
toate fisierele pe care clientul isi doreste sa le descarce.
1. Cere tracker-ului lista de seeds/peers pentru fisierul pe care isi
doreste sa il descarce. Acesta ii furnizeaza o lista cu toti clientii
ce detin fisierul si se retine in variabila final_chunk numarul de
segmente in care este impartit fisierul.
2. Incepe sa trimita cereri catre peer/seed pentru descarcarea fiecarui
segment. Pentru a asigura eficienta programului, se cicleaza prin toti 
clientii ce detin segmentul dorit. Astfel, pentru primul segment se va 
trimite o cerere catre primul peer/seed din lista de la tracker, iar
pentru urmatoarele segmente se avanseaza in lista. Cand se ajunge la 
finalul listei, se reiau cererile de la inceputul acesteia.
3. Pentru fiecare segment dorit se realizeaza pasii:
a. Se alege un seed/peer care detine segmentul cautat (initial se alege 
primul din lista)
b. Se trimite o cerere pentru descarcare cu numele fisierului, respectiv
segmentul dorit. 
c. Se asteapta primirea hash-ului de la peer/seed.
d. Se adauga segmentul in structura fisierului din variabila
download_files.

### Actualizare

Aceasta este implementata in functia download_thread_func.
1. Dupa 10 segmente descarcate (contorizate folosind variabila update),
se trimite o actualizare catre tracker. Astfel, se trimite structura cu
fisierul ce este in proces de descarcare cu segmentele pe care le detine
la momentul curent.
2. Se reiau toti pasii de la sectiunea Descarcare pentru a updata lista
de peers/seeds.

### Primire de mesaje de la alti clienti

Aceasta este implementata in functia upload_thread_func.
Atunci cand primeste un mesaj de la un client cu numele fisierului
si segmentul dorit, cauta in lista sa de fisiere hash-ul corespunzator.
Dupa ce este gasit, acesta este trimis inapoi la clientul care a facut
cererea.

### Finalizare descarcare fisier

Aceasta este implementata in functia download_thread_func.
1. Informeaza tracker-ul ca are tot fisierul descarcat, trimitand
structura fisierului cu toate segmentele acestuia.
2. Salveaza fisierul descarcat intr-un fisier de iesire si se continua
descarcarea urmatorului fisier.

### Finalizare descarcare toate fisierele

Aceasta este implementata in functia download_thread_func.
1. Ii trimite tracker-ului informatia ca a terminat toate descarcarile
printr-un mesaj de tip "TERMINATED".
2. Se inchide firul de executie de download.

### Finalizare pentru toti clientii

Aceasta este implementata in functia upload_thread_func, cand clientul
primeste de la tracker un mesaj cu informatia "STOP". In acel moment,
se inchide firul de executie de upload si, automat, se inchide si clientul.

### 2. Detalii implementare tracker

### Initializare

Implementarea este realizata in functia tracker. Variabila has_files retine
pentru fiecare fisier toti clientii care il detin. Variabila total_files
retine pentru fiecare client toate fisierele/ segmentele pe care le detine
la momentul curent.
1. Se asteapta mesajul initial de la fiecare client cu numarul de fisiere
detinute initial.
2. Se asteapta de la fiecare client mesaje cu informatiile fiecarui fisier
detinut, care vor fi salvate in variabilele has_files si total_files.
3. Dupa ce a primit toate informatiile de la fiecare client, trimite cate un
mesaj cu "ACK" catre fiecare.

### Primire de mesaje de la clienti

Implementarea este realizata in functia tracker. Acesta poate primi 4
tipuri de mesaje, specificate in enum-ul Message (DOWNLOAD, UPDATE, FINISHED,
TERMINATED). Tracker-ul contorizeaza, de asemenea, numarul de clienti care
si-au terminat descarcarea. Astfel, cand toti au terminat de descarcat,
va trimite catre fiecare client un mesaj "STOP" pentru a isi incheia executia.
Daca mesajul primit este de tip DOWNLOAD, tracker-ul va trimite o lista cu
toti clientii care detin fisierul dorit. Astfel, el va primi numele fisierului
si va intoarce numarul de peers/seeds care il detin, precum si datele fiecarui
fisier (cine il detine - pentru a sti cui sa trimita cererea de download, numarul
de segmente detinute si segmentele in sine). Daca mesajul primit este de tip
UPDATE, va primi o actualizare a fisierului ce este in curs de descarcare, ce 
contine numele fisierului, numarul de segmente detinute la momentul curent si 
segmentele in sine, iar tracker-ul va actualiza informatiile din variabilele
sale. Daca mesajul primit este de tip FINISHED, tracker-ul va primi fisierul
ce s-a terminat de descarcat (si toate informatiile sale) si isi va reactualiza
variabilele. Daca mesajul primit este de tip TERMINATED, tracker-ul va 
incrementa numarul de clienti care si-au terminat descarcarea.