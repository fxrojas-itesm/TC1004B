USE Books;

--SELECT * FROM Home;
--SELECT * FROM Library;
--SELECT * FROM Store;

/*
-- LEFT OUTER JOIN Baldor, Aritmetica en Biblioteca
SELECT Author, Title, Classification
FROM Home
LEFT JOIN Library
    ON Home.ISBN = Library.ISBN
WHERE Author = 'Aurelio Baldor';

-- RIGHT OUTER JOIN Baldor, Aritmetica en Biblioteca
SELECT Author, Title, Classification
FROM Home
RIGHT JOIN Library
    ON Home.ISBN = Library.ISBN
WHERE Author = 'Aurelio Baldor';


-- INNER JOIN Baldor, Aritmetica en Biblioteca
SELECT Author, Title, Classification
FROM Home
INNER JOIN Library
    ON Home.ISBN = Library.ISBN
WHERE Author = 'Aurelio Baldor';
*/

/*
-- LEFT OUTER JOIN Baldor, Aritmetica en Biblioteca
SELECT Author, Title, Classification
FROM Home
LEFT JOIN Library
    ON Home.ISBN = Library.ISBN
WHERE Author = 'Donald A. Neamen';

-- RIGHT OUTER JOIN Baldor, Aritmetica en Biblioteca
SELECT Author, Title, Classification
FROM Home
RIGHT JOIN Library
    ON Home.ISBN = Library.ISBN
WHERE Author = 'Donald A. Neamen';


-- INNER JOIN Baldor, Aritmetica en Biblioteca
SELECT Author, Title, Classification
FROM Home
INNER JOIN Library
    ON Home.ISBN = Library.ISBN
WHERE Author = 'Donald A. Neamen';
*/

--  Find out how many Oreilly books that exist in
-- home also exist in library
/*
SELECT Author, Title, Publisher
FROM Home
INNER JOIN Library
    ON Home.ISBN = Library.ISBN
WHERE Publisher = 'OReilly Media';
*/

-- Show all books in Home from 2020 onwards
/*
SELECT *
FROM Home
WHERE Year >= 2020
ORDER BY Year ASC;
*/

-- In Home count the number of books on their first edition
/*
SELECT COUNT(ID) AS First_edition_books
FROM Home
WHERE Edition = 1;
*/




-- In Home library count the number of books by publisher
/*
SELECT Publisher, COUNT(ID) AS Number_of_book_by_publisher
FROM Home
GROUP BY Publisher;
*/

-- Show List price and with a 20% discount on Book 5 from Home table
SELECT Title, Price, (Price*0.8) AS Discounted_20_price
FROM Home
INNER JOIN Store
    ON Home.ISBN = Store.ISBN
WHERE Home.ID = 5
    OR Home.ID = 8
    OR Home.ID = 2;
