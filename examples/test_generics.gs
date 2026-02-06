struct Node[T]:
    val: T
    next: ptr Node[T]

class LinkedList[T]:
    head: ptr Node[T]

    def init(self):
        self.head = nil

    def push(self, val: T):
        let newNode = new Node[T]()
        newNode.val = val
        newNode.next = self.head
        self.head = newNode

    def printAll(self):
        let curr = self.head
        while curr != nil:
            println(curr.val)
            curr = curr.next

fn main():
    let list = new LinkedList[int]()
    list.push(10)
    list.push(20)
    list.push(30)
    list.printAll()
