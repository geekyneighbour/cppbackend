import os
import random
import time
import types
import uuid

import pytest
import select
import subprocess

from typing import Callable, Optional, List, Union, Dict
from dataclasses import dataclass
from contextlib import contextmanager

import psycopg2
from psycopg2.extras import DictCursor
from psycopg2.extensions import ISOLATION_LEVEL_AUTOCOMMIT
from psycopg2 import errors


random.seed(42)

# TODO:
#  Replace all time.sleep
#  Replace book_tags with tags after fixing in current solution


# TODO:
#   DeleteAuthor
#       By name      - Ok
#       By index     - Ok
#       Canceling    - Failed to delete
#       Non-existent - Ok
#   EditAuthor
#       By Name      - Ok
#       By Index     - Ok
#       Canceling    - Failed to edit
#       Non-existing - Ok
#   AddBook
#       Normal       -
#       Without tags -
#       No author    -
#   ShowBooks
#       Ok
#   ShowBook
#       By name      - Ok
#       By index     - Ok
#       Gl index     - Ok
#       Canceling    - Ok
#       Non-existing - Shows empty line instead of 'Book not found'
#   DeleteBooks
#       By name      - Ok
#       By index     - Ok
#       Gl index     - Ok
#       Canceling    - Ok
#       Non-existing - Shows empty line instead of 'Book not found'
#   EditBook
#       By name      - Ok
#       By index     - Ok
#       Gl index     - Ok
#       Partial      - Of with quick-hack (Clean tags clearing the tags (instead of using current tags), which brakes the db and the query)
#       Canceling    - Book not found
#       Non-existing - Ok
#   Concurrency
#       AddBook      - Ok
#       EditBook     - Ok


def create_random_name() -> str:
    names = [
        'Bob', 'Kevin', 'Roberto', 'Quill', 'Patrick', 'Luciano', 'Zelda', 'Patricia', 'Sara'
    ]
    surnames = [
        'Fox', 'Gallagher', 'The Bull', 'The third', 'Spalletti', 'Brown', 'The greatest', ''
    ]
    additions = [
        '',  'Great', 'Superpowered', 'Fictional', 'Egocentric'
    ]
    author: str = random.choice(additions) + ' ' + random.choice(names) + ' ' + random.choice(surnames)
    author = author.strip()
    return author


def create_new_name(db_name) -> str:
    attempt = create_random_name()
    try:
        authors = [pair[1] for pair in get_authors(db_name)]
    except psycopg2.errors.UndefinedTable:
        authors = []

    while attempt in authors:
        attempt = create_random_name()

    return attempt


def create_messy_tags(count: Optional[int] = 20) -> str:
    tags = ['tag 1', 'romantic', 'pedantic', 'horror', 'for adults', 'for children']
    bad_tags = ['', '      ', '    spaaaaces   ', ', , , , ', ' #']
    bad_count = random.randint(0, count - 1)
    chosen = [random.choice(tags) for _ in range(count - bad_count)]
    chosen.extend([random.choice(bad_tags) for _ in range(bad_count)])
    chosen.append(random.choice(chosen))  # To ensure there is at least one duplicate
    result = ''
    for tag in chosen:
        result += tag + ', '
    return result


def clear_tags(tags: str) -> str:
    tag_list = list(set([tag.strip() for tag in tags.split(',')]))
    tag_list.sort()

    tag_str = ''
    for tag in tag_list:
        if tag == '':
            continue
        tag_str += tag + ', '
    if len(tag_str) > 0:
        tag_str = tag_str[:-2]
    return tag_str


def get_connection(db_name):
    return psycopg2.connect(user=os.environ['POSTGRES_USER'],
                            password=os.environ['POSTGRES_PASSWORD'],
                            host=os.environ['POSTGRES_HOST'],
                            port=os.environ['POSTGRES_PORT'],
                            dbname=db_name,
                            cursor_factory=DictCursor,
                            )


def authors_to_str(authors):
    return [f'{i+1} {author["name"]}'.strip() for i, author in enumerate(authors)]


def books_to_str(books):
    return [f'{i+1} {book["title"]} by {book["name"]}, {book["publication_year"]}'.strip() for i, book in enumerate(books)]


def book_to_str(book: List[str]) -> List[str]:
    result = list()
    result.append('Title: ' + book[1])
    result.append('Author: ' + book[2])
    result.append('Publication year: ' + str(book[3]))
    if len(book) == 5:
        tags = book[4].split(',')
        tags = [tag.strip() for tag in tags]
        tags.sort()
        sorted_tags = ''
        for tag in tags:
            sorted_tags += tag + ', '
        sorted_tags = sorted_tags[:-2]  # Dumb way to get rid of ", " on the end
        result.append('Tags: ' + sorted_tags)

    return result


def get_authors(db_name):
    query = "SELECT id, name FROM authors ORDER BY name;"
    with get_connection(db_name) as conn:
        with conn.cursor() as cur:
            cur.execute(query)
            return cur.fetchall()


def get_books(db_name):
    query = """SELECT
    books.id AS book_id,
    title,
    author_id,
    authors.name AS name,
    publication_year
FROM books
INNER JOIN authors ON authors.id = author_id
ORDER BY title, name, publication_year
;"""
    with get_connection(db_name) as conn:
        with conn.cursor() as cur:
            cur.execute(query)
            return cur.fetchall()


def get_author_books(db_name, author_id):
    query = """SELECT
    id,
    title,
    author_id,
    publication_year 
FROM books
WHERE author_id=%s
ORDER BY publication_year, title
;"""
    with get_connection(db_name) as conn:
        with conn.cursor() as cur:
            cur.execute(query, (author_id,))
            return [f'{i+1} {book["title"]}, {book["publication_year"]}'.strip() for i, book in enumerate(cur.fetchall())]


def get_book(db_name, title):

    query = """SELECT
            books.id AS book_id,
            title,
            name,
            publication_year,
            grouped_tags.tags
        FROM books
        INNER JOIN authors ON authors.id = author_id
        INNER JOIN (
            SELECT
                book_id,
                STRING_AGG(tag, ', ') AS tags
            FROM book_tags
            GROUP BY book_tags.book_id
        ) AS grouped_tags
        ON grouped_tags.book_id = books.id
        WHERE title=%s
        ;"""

    tagless_query = """SELECT
            books.id AS book_id,
            title,
            name,
            publication_year
        FROM books
        INNER JOIN authors ON authors.id = author_id
        WHERE title=%s
        ;"""

    with get_connection(db_name) as conn:
        with conn.cursor() as cur:
            cur.execute(query, (title,))
            res = cur.fetchall()
            if res == []:
                cur.execute(tagless_query, (title,))
                res = cur.fetchall()

    return res


@dataclass
class Bookypedia:
    process: subprocess.Popen
    db_name: str
    chose: Optional[str] = None

    @staticmethod
    def random_chooser(authors):
        index = random.randint(0, len(authors)-1)
        return index+1, authors[index]

    @staticmethod
    def index_chooser(index: int):
        def chooser(authors):
            return index+1, authors[index]
        return chooser

    @staticmethod
    def empty_chooser(authors):  # To test canceling
        return '', ''

    @staticmethod
    def get_index_by_author(db_name, author):
        authors = authors_to_str(get_authors(db_name))
        for i, string in enumerate(authors):
            if string.find(author) != -1:
                return i + 1

    @staticmethod
    def author_chooser(author):
        def chooser(authors: List[str]):
            for i, line in enumerate(authors):
                if line.find(author) != -1:
                    return i + 1, author
        return chooser

    @staticmethod
    def book_chooser(author):
        def chooser(books: List[str]):
            for i, book in enumerate(books):
                if book.find(author) != -1:
                    return i + 1, book
        return chooser

    def _wait_select(self, timeout=0.2):
        delta = timeout/100
        for _ in range(100):
            if select.select([self.process.stdout], [], [], 0.0)[0]:
                return True
            time.sleep(delta)
        return False

    def _wait_strings(self, timeout=0.2):
        res = list()
        if self._wait_select(timeout):
            delta = timeout/100
            for _ in range(100):
                line = self.process.read()
                if line:
                    res.append(line)
                time.sleep(delta)
        return res

    def add_author(self, author: Optional[str]) -> Optional[str]:
        if author:
            self.process.write(f'AddAuthor {author}')
        else:
            self.process.write(f'AddAuthor')
        if self._wait_select():
            return self.process.read()

    def show_authors(self) -> List[str]:
        self.process.write('ShowAuthors')
        return self._wait_strings()

    def _author(self, command, author_or_callback: Union[str, Callable]) -> Optional[str]:
        if isinstance(author_or_callback, str):
            self.process.write(f'{command} {author_or_callback}')
        else:
            self.process.write(command)
            authors = self._wait_strings()[1:-1]
            if authors:
                number, self.chose = author_or_callback(authors)
                self.process.write(f'{number}')
            else:
                self.process.write('')
                return None
        if self._wait_select():
            return self.process.read()

    def delete_author(self, author_or_callback: Union[str, Callable]) -> Optional[str]:
        return self._author('DeleteAuthor', author_or_callback)

    def edit_author(self, author_or_callback: Union[str, Callable], new_author: str) -> Optional[str]:
        request = self._author('EditAuthor', author_or_callback)  # Enter new name:
        if author_or_callback == self.empty_chooser:
            return request
        if request.startswith('Failed'):
            return request
        self.process.write(new_author)
        if self._wait_select():
            return self.process.read()

    def _book(self, command: str, book: Optional[str], callback: Optional[Callable]) -> List[str]:

        # Using title
        if isinstance(book, str):
            self.process.write(f'{command} {book}')
            books = self._wait_strings()
            if books:
                # More than one book with this title
                if books[0].startswith('1'):
                    if callback:
                        number, self.chose = callback(books)
                    else:
                        self.chose = books[0]
                        number = 1
                    self.process.write(f'{number}')
                else:
                    return books
        # Using callback only
        else:
            self.process.write(command)
            books = self._wait_strings()[:-1]
            if books:
                number, self.chose = callback(books)
                self.process.write(f'{number}')

        return self._wait_strings()

    def __book(self, command: str, book_or_callback: Union[str, Callable]) -> List[str]:
        if isinstance(book_or_callback, str):
            self.process.write(f'{command} {book_or_callback}')
            books = self._wait_strings()
            if books:
                if books[0].startswith('1'):
                    self.chose = books[0]
                    self.process.write('1')
                else:
                    return books
        else:
            self.process.write(command)
            books = self._wait_strings()[:-1]
            if books:
                number, self.chose = book_or_callback(books)
                self.process.write(f'{number}')
        return self._wait_strings()

    def show_book(self, book: Optional[str] = None, callback: Optional[Callable] = None) -> List[str]:
        return self._book('ShowBook', book, callback)

    def delete_book(self, book: Optional[str] = None, callback: Optional[Callable] = None) -> List[str]:
        return self._book('DeleteBook', book, callback)

    def edit_book(self, book: Optional[str] = None, callback: Optional[Callable] = None, new_info: Dict[str, str] = None) -> List[str]:
        mb_title = self._book('EditBook', book, callback)  # Enter new title
        if mb_title:
            if not mb_title[0].startswith('Enter new title'):
                return mb_title[0]
        self.process.write(new_info['title'])

        _ = self._wait_strings(0.1)  # Enter publication year
        self.process.write(new_info['year'])
        _ = self._wait_strings(0.1)  # Enter tags
        self.process.write(new_info['tags'])

        if self._wait_select():
            return self.process.read()

    def add_book(self, year: int, title: str, author_or_callback: Union[str, Callable], tags: str, add_author_answer: str = 'y') -> Optional[str]:
        self.process.write(f'AddBook {year} {title}')
        if self._wait_select():
            _ = self.process.read()  # Enter author

        if isinstance(author_or_callback, str):
            self.process.write(author_or_callback)
            if self._wait_select():
                _ = self.process.read()  # y/n?
                self.process.write(add_author_answer)
                if add_author_answer != 'y' and add_author_answer != 'Y':
                    return self._wait_strings()[0]

        else:
            self.process.write('')
            authors = self._wait_strings()[1:-1]
            if authors:
                number, self.chose = author_or_callback(authors)
                self.process.write(f'{number}')
            else:
                self.process.write('')

        if self._wait_select():
            _ = self.process.read()   # Add tags
            self.process.write(tags)

        if self._wait_select():
            return self.process.read()

    def show_author_books(self, author_chooser: Optional[Callable] = None) -> List[str]:
        if author_chooser is None:
            author_chooser = Bookypedia.random_chooser
        self.process.write('ShowAuthorBooks')
        authors = self._wait_strings()[1:-1]
        if authors:
            number, self.chose = author_chooser(authors)
            self.process.write(f'{number}')
        else:
            self.process.write('')
        return self._wait_strings()

    def show_books(self) -> List[str]:
        self.process.write('ShowBooks')
        return self._wait_strings()


@contextmanager
def run_bookypedia(db_name, terminate=True, reset_db=True):
    def _read(self):
        return self.stdout.readline().strip()

    def _write(self, message: str):
        self.stdin.write(f"{message.strip()}\n")
        self.stdin.flush()

    def _terminate(self):
        self.stdin.close()
        self.terminate()
        self.wait()

    try:
        if reset_db:
            drop_db(db_name)
            create_db(db_name)
    except Exception:
        pass

    db_connect = f"postgres://{os.environ['POSTGRES_USER']}:{os.environ['POSTGRES_PASSWORD']}@{os.environ['POSTGRES_HOST']}:{os.environ['POSTGRES_PORT']}/{db_name}"
    proc = subprocess.Popen(os.environ['DELIVERY_APP'].split(), text=True, env=dict(os.environ, BOOKYPEDIA_DB_URL=db_connect),
                            stdout=subprocess.PIPE, stdin=subprocess.PIPE)
    os.set_blocking(proc.stdout.fileno(), False)
    proc.write = types.MethodType(_write, proc)
    proc.read = types.MethodType(_read, proc)
    proc.new_terminate = types.MethodType(_terminate, proc)

    try:
        yield Bookypedia(proc, db_name)
    finally:
        if terminate:
            proc.new_terminate()


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_add_author(db_name):
    with run_bookypedia(db_name) as bookypedia:
        if db_name != 'full_db':
            assert bookypedia.add_author(None).startswith('Failed to add author')
            assert bookypedia.add_author('author2') is None
            assert bookypedia.add_author('author1') is None
            assert bookypedia.add_author('author2').startswith('Failed to add author')
        else:
            assert bookypedia.add_author(None).startswith('Failed to add author')
            assert bookypedia.add_author('author2').startswith('Failed to add author')
            assert bookypedia.add_author('author1').startswith('Failed to add author')
            assert bookypedia.add_author('author2').startswith('Failed to add author')


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_show_authors(db_name):
    with run_bookypedia(db_name) as bookypedia:
        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))

        bookypedia.add_author('author3')
        bookypedia.add_author('author2')
        bookypedia.add_author('author1')

        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_delete_existing_author_by_name(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        extra_authors = [create_new_name(db_name) for _ in range(0, 3)]

        for author in extra_authors:
            assert bookypedia.add_author(author) is None
        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))

        for author in get_authors(db_name):
            assert bookypedia.delete_author(author[1]) is None

        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))


# @pytest.mark.skip
@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_delete_existing_author_by_index(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        # without books
        extra_authors = [create_new_name(db_name) for _ in range(0, 3)]

        for author in extra_authors:
            assert bookypedia.add_author(author) is None
        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))

        for _ in get_authors(db_name):
            assert bookypedia.delete_author(bookypedia.random_chooser) is None

        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_delete_author_canceling(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        # without books
        extra_authors = [create_new_name(db_name) for _ in range(0, 3)]

        for author in extra_authors:
            assert bookypedia.add_author(author) is None
        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))

        result = bookypedia.delete_author(bookypedia.empty_chooser)
        assert result is None or result.startswith('Failed to delete author')    # Returns Failed to delete author

        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_delete_non_existing_author(db_name):
    with run_bookypedia(db_name, reset_db=True) as bookypedia:
        bookypedia: Bookypedia

        non_existing_authors = [create_new_name(db_name) for _ in range(0, 3)]

        for pseudo_author in non_existing_authors:
            assert bookypedia.delete_author(pseudo_author).startswith('Failed to delete author')

        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_edit_author_by_name(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        # without books
        extra_authors = [create_new_name(db_name) for _ in range(0, 3)]

        for author in extra_authors:
            assert bookypedia.add_author(author) is None
        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))

        if db_name != 'empty_db':
            books = bookypedia.show_author_books(bookypedia.author_chooser(author))

        new_name: str = create_new_name(db_name)
        assert bookypedia.edit_author(author, new_name) is None
        if db_name != 'empty_db':
            assert books == bookypedia.show_author_books(bookypedia.author_chooser(new_name))

        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_edit_author_by_index(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        extra_authors = [create_new_name(db_name) for _ in range(0, 3)]

        for author in extra_authors:
            assert bookypedia.add_author(author) is None
        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))

        author = random.choice(get_authors(db_name))[1]

        if db_name != 'empty_db':
            books = bookypedia.show_author_books(bookypedia.author_chooser(author))

        new_name: str = create_new_name(db_name)
        assert bookypedia.edit_author(bookypedia.author_chooser(author), new_name) is None
        if db_name != 'empty_db':
            assert books == bookypedia.show_author_books(bookypedia.author_chooser(new_name))

        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_edit_author_canceling(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        extra_authors = [create_new_name(db_name) for _ in range(0, 3)]

        for author in extra_authors:
            assert bookypedia.add_author(author) is None
        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))

        author = random.choice(get_authors(db_name))[1]

        if db_name != 'empty_db':
            books = bookypedia.show_author_books(bookypedia.author_chooser(author))

        result = bookypedia.edit_author(bookypedia.empty_chooser, None)
        assert result is None or result.startswith('Failed to edit author')

        if db_name != 'empty_db':
            assert books == bookypedia.show_author_books(bookypedia.author_chooser(author))

        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_edit_non_existing_author(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        # without books
        extra_authors = [create_new_name(db_name) for _ in range(0, 3)]

        for author in extra_authors:
            assert bookypedia.add_author(author) is None
        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))

        new_name: str = create_new_name(db_name)
        res = bookypedia.edit_author(new_name, new_name)
        assert res.startswith('Failed to edit author')

        assert bookypedia.show_authors() == authors_to_str(get_authors(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_add_book(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        titles = [
            'How to cook an egg', 'Great man cannot do great things!', 'Strange ancient book!'
        ]

        # New author, with tags
        year = random.randint(1, 2054)
        author = create_new_name(db_name)
        tags = create_messy_tags(15)
        book = [
            titles[0],
            author,
            year,
            clear_tags(tags)
            ]

        result = bookypedia.add_book(year, titles[0], author, tags, 'y')

        assert result is None
        assert bookypedia.show_books() == books_to_str(get_books(db_name))
        db_book: list = get_book(db_name, titles[0])[0]
        assert db_book[1:] == book

        # Old author, with tags

        authors = [pair[1] for pair in get_authors(db_name)]
        author = random.choice(authors)
        year = random.randint(1, 2054)
        tags = create_messy_tags(15)
        book = [
            titles[1],
            author,
            year,
            clear_tags(tags)
            ]

        result = bookypedia.add_book(year, titles[1], bookypedia.author_chooser(author), tags)

        assert result is None
        assert bookypedia.show_books() == books_to_str(get_books(db_name))
        db_book: list = get_book(db_name, titles[1])[0]
        assert db_book[1:] == book

        # New author, without tags

        author = create_new_name(db_name)
        year = random.randint(1, 2054)
        book = [
            titles[2],
            author,
            year,
            ]

        result = bookypedia.add_book(year, titles[2], author, tags='')

        assert result is None
        assert bookypedia.show_books() == books_to_str(get_books(db_name))
        db_book: list = get_book(db_name, titles[2])[0]
        assert db_book[1:] == book

        # Empty author to cancel

        year = random.randint(1, 2054)
        tags = create_messy_tags(15)

        result = bookypedia.add_book(year, titles[1], bookypedia.empty_chooser, tags)
        assert result is None or result.startswith('Failed to add book')
        assert bookypedia.show_books() == books_to_str(get_books(db_name))

        # New author, not adding, so failed to add

        author = create_new_name(db_name)
        year = random.randint(1, 2054)

        result = bookypedia.add_book(year, titles[2], author, tags='', add_author_answer='n')

        assert result.startswith('Failed to add book')
        assert bookypedia.show_books() == books_to_str(get_books(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_show_books(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        bookypedia.add_book(1563, 'How to cook an egg', bookypedia.random_chooser, 'tag23, tag14, tag 55')
        bookypedia.add_book(8763, 'Great man cannot do great things!', create_new_name(db_name), 'y')

        bookypedia.add_book(74, 'Strange ancient book!', create_new_name(db_name), 'tag1, tag2, tag3', 'y')

        books = bookypedia.show_books()
        db_books = books_to_str(get_books(db_name))

        assert books == db_books


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_show_book(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        books = [
            'How to cook an egg', 'Great man cannot do great things!', 'Strange ancient book!'
        ]
        bookypedia.add_book(random.randint(1, 2054), books[0], create_new_name(db_name), create_messy_tags(15))
        bookypedia.add_book(random.randint(1, 2054), books[1], bookypedia.random_chooser, create_messy_tags(15))
        bookypedia.add_book(random.randint(1, 2054), books[2], create_new_name(db_name), tags='')

        for title in books:
            db_book = book_to_str(get_book(db_name, title)[0])

            bookypedia_book = bookypedia.show_book(title)
            assert db_book == bookypedia_book


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_show_book_choose_book(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        book_title = 'A new way to cook eggs!'
        authors = [create_new_name(db_name) for _ in range(3)]

        for author in authors:
            bookypedia.add_book(random.randint(1, 2054), book_title, author, create_messy_tags(15))

        for author in authors:

            db_books = get_book(db_name, book_title)
            for book in db_books:
                if book[2] == author:
                    break
            else:
                book = None
            book = book_to_str(book)
            bookypedia_book = bookypedia.show_book(book_title, bookypedia.book_chooser(author))
            assert bookypedia_book == book


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_show_book_by_global_index(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        books = [
            'How to cook an egg', 'Great man cannot do great things!', 'Strange ancient book!'
        ]
        for book in books:
            bookypedia.add_book(random.randint(1, 2054), book, create_new_name(db_name), create_messy_tags(15))
        db_books = get_books(db_name)
        string_books = bookypedia.show_books()
        for i, book in enumerate(string_books):
            for b in db_books:
                if book.find(b[1] + ' by') != -1:
                    title = b[1]
                    break
            db_dict_book = get_book(db_name, title)[0]
            db_book = book_to_str(db_dict_book)
            bookypedia_book = bookypedia.show_book(callback=bookypedia.index_chooser(i))
            assert bookypedia_book == db_book


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_show_non_existing_book(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        books = [
            'How to cook an egg', 'Great man cannot do great things!', 'Strange ancient book!'
        ]

        for title in books:
            res = bookypedia.show_book(title)
            if len(res):
                assert res[0].startswith('Book not found')
            else:
                assert res == []


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_show_book_canceling(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        book_title = 'A new way to cook eggs!'
        authors = [create_new_name(db_name) for _ in range(3)]

        for author in authors:
            bookypedia.add_book(random.randint(1, 2054), book_title, author, create_messy_tags(15))
        authors.sort()

        assert bookypedia.show_book(book_title, bookypedia.empty_chooser) == []
        assert bookypedia.show_book(callback=bookypedia.empty_chooser) == []


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_delete_book(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        book_titles = [
            'How to cook an egg', 'Great man cannot do great things!', 'Strange ancient book!'
        ]
        for book in book_titles:
            bookypedia.add_book(random.randint(1, 2054), book, create_new_name(db_name), create_messy_tags(15))

        books = get_books(db_name)

        for book in books:
            title = book[1]
            assert bookypedia.delete_book(title) == []
        assert books_to_str(get_books(db_name)) == bookypedia.show_books()


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_delete_book_choose_book(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        book_title = 'A new way to cook eggs!'
        authors = [create_new_name(db_name) for _ in range(3)]

        for author in authors:
            bookypedia.add_book(random.randint(1, 2054), book_title, author, create_messy_tags(15))

        books = get_books(db_name)
        for book in books:
            title = book[1]
            author = book[3]
            assert bookypedia.delete_book(title, bookypedia.book_chooser(author)) == []
        assert books_to_str(get_books(db_name)) == bookypedia.show_books()


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_delete_book_by_global_index(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        books = [
            'How to cook an egg', 'Great man cannot do great things!', 'Strange ancient book!'
        ]
        for book in books:
            bookypedia.add_book(random.randint(1, 2054), book, create_new_name(db_name), create_messy_tags(15))
        for _ in get_books(db_name):
            bookypedia.delete_book(callback=bookypedia.index_chooser(0))

        assert bookypedia.show_books() == books_to_str(get_books(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_delete_non_existing_book(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        book_titles = [
            'How to cook an egg', 'Great man cannot do great things!', 'Strange ancient book!'
        ]

        for title in book_titles:
            res = bookypedia.delete_book(title)
            if len(res):
                assert res[0].startswith('Book not found')
            else:
                assert res == []
        assert bookypedia.show_books() == books_to_str(get_books(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_delete_book_canceling(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        book_title = 'A new way to cook eggs!'
        authors = [create_new_name(db_name) for _ in range(3)]

        for author in authors:
            bookypedia.add_book(random.randint(1, 2054), book_title, author, create_messy_tags(15))
        authors.sort()

        assert bookypedia.delete_book(book_title, bookypedia.empty_chooser) == []
        assert bookypedia.delete_book(callback=bookypedia.empty_chooser) == []

        assert bookypedia.show_books() == books_to_str(get_books(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_edit_book_by_name(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        books = [
            'How to cook an egg', 'Great man cannot do great things!', 'Strange ancient book!'
        ]
        for book in books:
            bookypedia.add_book(random.randint(1, 2054), book, create_new_name(db_name), create_messy_tags(15))
        books = get_books(db_name)
        for book in books:
            db_book = book_to_str(get_book(db_name, book[1])[0])
            bookypedia_book = bookypedia.show_book(book[1])
            assert db_book == bookypedia_book

            new_author = create_new_name(db_name)
            new_title = f'Biography of {new_author}'
            new_tags = create_messy_tags()
            new_year = random.randint(1, 6531)
            new_info = {
                'title': new_title,
                'year': str(new_year),
                'tags': new_tags
            }
            bookypedia.edit_book(book=book[1], new_info=new_info)

            db_book = book_to_str(get_book(db_name, new_title)[0])
            bookypedia_book = bookypedia.show_book(new_title)
            assert bookypedia_book == db_book

        assert bookypedia.show_books() == books_to_str(get_books(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_edit_book_choose_book(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        book_title = 'A new way to cook eggs!'
        authors = [create_new_name(db_name) for _ in range(3)]

        for author in authors:
            bookypedia.add_book(random.randint(1, 2054), book_title, author, create_messy_tags(15))

        chosen_author = random.choice(authors)

        db_books = get_book(db_name, book_title)
        for db_book in db_books:
            if db_book[2] == chosen_author:
                break
        else:
            db_book = None  # Something went wrong and an exceptions will be raised soon

        bookypedia_book = bookypedia.show_book(book_title, bookypedia.book_chooser(chosen_author))
        assert bookypedia_book == book_to_str(db_book)

        new_title = f'Biography of {author}'
        new_tags = create_messy_tags()
        new_year = random.randint(1, 6531)
        new_info = {
            'title': new_title,
            'year': str(new_year),
            'tags': new_tags
        }

        bookypedia.edit_book(book_title, bookypedia.book_chooser(chosen_author), new_info)
        time.sleep(5)

        db_book = book_to_str(get_book(db_name, new_title)[0])
        bookypedia_book = bookypedia.show_book(new_title)
        assert bookypedia_book == db_book
        assert bookypedia.show_books() == books_to_str(get_books(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_edit_book_by_global_index(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        books = [
            'How to cook an egg', 'Great man cannot do great things!', 'Strange ancient book!'
        ]
        for book in books:
            bookypedia.add_book(random.randint(1, 2054), book, create_new_name(db_name), create_messy_tags(15))
        db_books = get_books(db_name)
        string_books = bookypedia.show_books()
        book = random.choice(db_books)
        title = book[1]
        for i, book_string in enumerate(string_books):
            if book_string.find(title + ' by') == -1:
                continue

            bookypedia_book = bookypedia.show_book(title)
            assert bookypedia_book == book_to_str(get_book(db_name, title)[0])

            new_title = f'Biography of somebody'
            new_tags = create_messy_tags()
            new_year = random.randint(1, 6531)
            new_info = {
                'title': new_title,
                'year': str(new_year),
                'tags': new_tags
            }

            bookypedia.edit_book(callback=bookypedia.index_chooser(i), new_info=new_info)

            db_book = book_to_str(get_book(db_name, new_title)[0])
            bookypedia_book = bookypedia.show_book(new_title)
            assert bookypedia_book == db_book
            assert bookypedia.show_books() == books_to_str(get_books(db_name))


@pytest.mark.parametrize('new_title', [True, False])
@pytest.mark.parametrize('new_year', [True, False])
@pytest.mark.parametrize('new_tags', [True, False])
@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_edit_book_parametrized(db_name, new_title, new_year, new_tags):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        books = [
            'How to cook an egg', 'Great man cannot do great things!', 'Strange ancient book!'
        ]
        for book in books:
            bookypedia.add_book(random.randint(1, 2054), book, create_new_name(db_name), create_messy_tags(15))
        db_books = get_books(db_name)
        string_books = bookypedia.show_books()

        book = random.choice(db_books)
        title = book[1]
        for i, book_string in enumerate(string_books):
            if book_string.find(title + ' by') == -1:
                continue
            index = i
            break

        db_book = get_book(db_name, title)[0]
        bookypedia_book = bookypedia.show_book(title)

        assert bookypedia_book == book_to_str(db_book)

        new_book_title = 'Biography of someone' if new_title else ' '

        new_info = {
            'title': new_book_title,
            'year': str(random.randint(1, 6531)) if new_year else ' ',
            'tags': create_messy_tags() if new_tags else ''
        }

        bookypedia.edit_book(callback=bookypedia.index_chooser(index), new_info=new_info)
        # FIXME: Tags should be kept if new were not provided. Current solution erases them if new were not present
        expected_book = [
            db_book[0],
            new_info['title'] if new_title else title,
            db_book[2],
            new_info['year'] if new_year else db_book[3],
        ]
        if new_tags:
            expected_book.append(clear_tags(new_info['tags']))

        new_db_book = book_to_str(get_book(db_name, expected_book[1])[0])
        new_bookypedia_book = bookypedia.show_book(expected_book[1])

        assert new_bookypedia_book == new_db_book
        assert new_bookypedia_book == book_to_str(expected_book)
        assert bookypedia.show_books() == books_to_str(get_books(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_edit_non_existing_book(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        books = [
            'How to cook an egg', 'Great man cannot do great things!', 'Strange ancient book!'
        ]

        for book in books:
            assert bookypedia.edit_book(book=book, new_info=None).startswith('Book not found')

        assert bookypedia.show_books() == books_to_str(get_books(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_edit_book_canceling(db_name):
    with run_bookypedia(db_name) as bookypedia:
        bookypedia: Bookypedia

        book_title = 'A new way to cook eggs!'
        authors = [create_new_name(db_name) for _ in range(3)]

        for author in authors:
            bookypedia.add_book(random.randint(1, 2054), book_title, author, create_messy_tags(15))

        result = bookypedia.edit_book(callback=bookypedia.empty_chooser, new_info=None)
        assert result is None or result.startswith('Book not found')

        result = bookypedia.edit_book(book_title, callback=bookypedia.empty_chooser, new_info=None)
        assert result is None or result.startswith('Book not found')

        assert bookypedia.show_books() == books_to_str(get_books(db_name))


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_concurrency_edit_book(db_name):
    with run_bookypedia(db_name) as bookypedia_1:
        bookypedia_1: Bookypedia

        books = [
            'How to cook an egg', 'Great man cannot do great things!', 'Strange ancient book!'
        ]
        for book in books:
            bookypedia_1.add_book(random.randint(1, 2054), book, create_new_name(db_name), create_messy_tags(15))

        book = random.choice(books)

        with run_bookypedia(db_name, reset_db=False) as bookypedia_2:
            bookypedia_2: Bookypedia

            old_books_1 = bookypedia_1.show_books()
            old_book_1 = bookypedia_1.show_book(book)

            def check_books():
                books_2 = bookypedia_2.show_books()
                books_db = books_to_str(get_books(db_name))
                book_2 = bookypedia_2.show_book(book)
                book_db = book_to_str(get_book(db_name, book)[0])
                assert old_books_1 == books_2 == books_db
                assert old_book_1 == book_2 == book_db

            check_books()

            _ = bookypedia_1._book('EditBook', book, callback=None)  # Enter new title

            new_info = {
                'title': 'Autobiography of Ted Mosby',
                'year': '2030',
                'tags': 'interesting, funny, over-detailed'

            }

            bookypedia_1.process.write(new_info['title'])
            check_books()
            _ = bookypedia_1._wait_strings(0.1)  # Enter publication year
            bookypedia_1.process.write(new_info['year'])
            check_books()
            _ = bookypedia_1._wait_strings(0.1)  # Enter tags
            bookypedia_1.process.write(new_info['tags'])

            new_books_1 = bookypedia_1.show_books()
            new_book_1 = bookypedia_1.show_book(new_info['title'])
            books_2 = bookypedia_2.show_books()
            books_db = books_to_str(get_books(db_name))
            book_2 = bookypedia_2.show_book(new_info['title'])
            book_db = book_to_str(get_book(db_name, new_info['title'])[0])
            assert new_books_1 == books_2 == books_db
            assert new_book_1 == book_2 == book_db


@pytest.mark.parametrize('db_name', ['empty_db', 'table_db', 'full_db'])
def test_concurrency_add_book(db_name):
    with run_bookypedia(db_name) as bookypedia_1:
        bookypedia_1: Bookypedia
        bookypedia_1.show_books()
        with run_bookypedia(db_name, reset_db=False) as bookypedia_2:
            bookypedia_2: Bookypedia

            info = {
                'title': 'Autobiography of Ted Mosby',
                'author': 'Ted Mosby',
                'year': '2030',
                'tags': 'interesting, funny, over-detailed'

            }

            old_books_1 = bookypedia_1.show_books()

            def check_books():
                books_2 = bookypedia_2.show_books()
                books_db = books_to_str(get_books(db_name))
                assert old_books_1 == books_2 == books_db
                assert get_book(db_name, info['title']) == []
                # assert bookypedia_2.show_book(info['title']).startswith('Book not found')
            check_books()

            bookypedia_1.process.write(f'AddBook {info["year"]} {info["title"]}')
            _ = bookypedia_1._wait_strings(0.1)
            bookypedia_1.process.write(info['author'])
            _ = bookypedia_1._wait_strings(0.1)
            bookypedia_1.process.write('y')
            _ = bookypedia_1._wait_strings(0.1)
            bookypedia_1.process.write(info['tags'])
            _ = bookypedia_1._wait_strings(0.1)

            new_books_1 = bookypedia_1.show_books()
            books_2 = bookypedia_2.show_books()
            books_db = books_to_str(get_books(db_name))
            assert new_books_1 == books_2 == books_db

            book_1 = bookypedia_1.show_book(info['title'])
            book_2 = bookypedia_2.show_book(info['title'])
            book_db = book_to_str(get_book(db_name, info['title'])[0])
            assert book_1 == book_2 == book_db


def drop_db(db_name):
    conn = get_connection(None)
    conn.set_isolation_level(ISOLATION_LEVEL_AUTOCOMMIT)
    with conn.cursor() as cur:
        try:
            cur.execute(f'drop database if exists {db_name};')
        except Exception as e:
            print(e)
    conn.close()


def create_db(db_name):
    conn = get_connection(None)
    conn.set_isolation_level(ISOLATION_LEVEL_AUTOCOMMIT)
    with conn.cursor() as cur:
        cur.execute(f'create database {db_name};')
    conn.close()

    if db_name in {'table_db', 'full_db'}:
        with get_connection(db_name) as conn:
            with conn.cursor() as cur:

                cur.execute(
                    """CREATE TABLE IF NOT EXISTS authors (
                        id UUID CONSTRAINT firstindex PRIMARY KEY,
                        name varchar(100) NOT NULL UNIQUE
                    );
                    CREATE TABLE IF NOT EXISTS books (
                        id UUID PRIMARY KEY,
                        title VARCHAR(100) NOT NULL,
                        publication_year INT,
                        author_id UUID,
                        CONSTRAINT fk_authors
                            FOREIGN KEY(author_id)
                            REFERENCES authors(id)
                    );
                    CREATE TABLE IF NOT EXISTS book_tags (
                        book_id UUID,
                        tag varchar(30) NOT NULL,
                        CONSTRAINT fk_books
                            FOREIGN KEY(book_id)
                            REFERENCES books(id)
                    );
                    """)

    insert_author = f'INSERT INTO authors (id, name) VALUES (%s, %s);'
    insert_book = f'INSERT INTO books (id, title, author_id, publication_year) VALUES (%s, %s, %s, %s);'
    insert_tag = f'INSERT INTO book_tags (book_id, tag) VALUES (%s, %s);'
    if db_name == 'full_db':
        with get_connection(db_name) as conn:
            with conn.cursor() as cur:

                author_ids = []
                for i in range(5):
                    _uuid = uuid.uuid4().hex
                    author_ids.append(_uuid)
                    cur.execute(insert_author, (_uuid, f'author{i}'))

                for i in range(20):
                    book_id = uuid.uuid4().hex
                    cur.execute(insert_book, (book_id, f'Title{i}', random.choice(author_ids), 1000+i))
                    for j in range(random.randint(0, 4)):
                        cur.execute(insert_tag, (book_id, f'tag{j}'))


if __name__ == '__main__':
    for name in ['empty_db', 'table_db', 'full_db']:
        drop_db(name)
        create_db(name)
