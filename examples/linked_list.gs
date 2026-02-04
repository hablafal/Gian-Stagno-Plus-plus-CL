class Node:
    def init(v):
        self.value = v
        self.next = cast<*Node>(nil)

fn main():
    head = Node(10)
    second = Node(20)
    third = Node(30)

    head.next = second
    second.next = third

    curr = head
    while cast<int>(curr) != 0:
        log("Node value: ", curr.value)
        curr = curr.next

    delete head
    delete second
    delete third
