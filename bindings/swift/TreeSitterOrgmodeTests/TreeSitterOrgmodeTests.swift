import XCTest
import SwiftTreeSitter
import TreeSitterOrgmode

final class TreeSitterOrgmodeTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_orgmode())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading org mode grammar")
    }
}
